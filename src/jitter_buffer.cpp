#include "srtc/jitter_buffer.h"
#include "srtc/depacketizer.h"
#include "srtc/logging.h"
#include "srtc/rtp_packet.h"
#include "srtc/track.h"

#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define LOG(level, ...) srtc::log(level, "JitterBuffer", __VA_ARGS__)

namespace
{

constexpr auto kNoPacketsResetDelay = std::chrono::milliseconds(2000);

// We calculate timeouts for the event loop (when there are no network events) with millisecond precision. When we
// time out and go to process actual logic like de-queuing frames or sending nacks, we need to use millisecond precision
// as well. If we don't, we might have a timeout = 0, but the actual logic won't run because it's 12 nanoseconds in the
// future or something. Then we'll end up cycling the event loop with 0 millisecond timeout value several times, which
// is bad for performance.

int diff_millis(const std::chrono::steady_clock::time_point& when, const std::chrono::steady_clock::time_point& now)
{
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(when - now + std::chrono::microseconds(500)).count());
}

} // namespace

namespace srtc
{

// Jitter Buffer

JitterBuffer::JitterBuffer(const std::shared_ptr<Track>& track,
                           const std::shared_ptr<Depacketizer>& depacketizer,
                           size_t capacity,
                           std::chrono::milliseconds length,
                           std::chrono::milliseconds nackDelay)
    : mTrack(track)
    , mDepacketizer(depacketizer)
    , mCapacity(capacity)
    , mCapacityMask(capacity - 1)
    , mLength(length)
    , mNackDelay(nackDelay)
    , mLastPacketTime(std::chrono::steady_clock::time_point::min())
    , mItemList(nullptr)
    , mMinSeq(0)
    , mMaxSeq(0)
    , mBaseTime(std::chrono::steady_clock::time_point::min())
    , mBaseRtpTimestamp(0)
{
    assert((mCapacity & mCapacityMask) == 0 && "capacity should be a power of 2");

    LOG(SRTC_LOG_V,
        "Constructed for %s with length = %ld, nack delay = %ld",
        to_string(mTrack->getMediaType()).c_str(),
        static_cast<long>(length.count()),
        static_cast<long>(nackDelay.count()));
}

JitterBuffer::~JitterBuffer()
{
    freeEverything();
}

std::shared_ptr<Track> JitterBuffer::getTrack() const
{
    return mTrack;
}

void JitterBuffer::consume(const std::shared_ptr<RtpPacket>& packet)
{
    assert(mTrack == packet->getTrack());

    auto seq = packet->getSequence();
    auto payload = packet->movePayload();

    if (packet->getSSRC() == mTrack->getRtxSSRC() && packet->getPayloadId() == mTrack->getRtxPayloadId()) {
        // Unwrap RTX
        if (payload.size() < 2) {
            LOG(SRTC_LOG_E, "RTX payload is less than 2 bytes, which can't be");
            return;
        }

        ByteReader reader(payload);
        seq = reader.readU16();

        payload = { payload.data() + 2, payload.size() - 2 };
    }

    // Extend
    const auto seq_ext = mExtValueSeq.extend(seq);
    const auto rtp_timestamp_ext = mExtValueRtpTimestamp.extend(packet->getTimestamp());

    // Is this packet too late?
    if (mLastFrameTimeStamp.has_value() && mLastFrameTimeStamp.value() > rtp_timestamp_ext) {
        LOG(SRTC_LOG_W,
            "Will not en-queue %s frame with ts = %" PRIu64 ", because it's older than last frame time %" PRIu64,
            to_string(mTrack->getMediaType()).c_str(),
            rtp_timestamp_ext,
            mLastFrameTimeStamp.value());
        return;
    }

    // Decide what to do
    const auto now = std::chrono::steady_clock::now();

    if (mItemList) {
        const auto elapsed = now - mLastPacketTime;
        if (elapsed >= kNoPacketsResetDelay && seq_ext >= mMaxSeq + mCapacity / 8) {
            LOG(SRTC_LOG_E,
                "We have not had %s packets for %ld milliseconds, resetting the jitter buffer",
                to_string(mTrack->getMediaType()).c_str(),
                static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
            freeEverything();
            mDepacketizer->reset();
        }
    }
    mLastPacketTime = now;

    if (mItemList == nullptr) {
        // First packet
        mMinSeq = seq_ext;
        mMaxSeq = mMinSeq + 1;

        mItemList = new JitterBufferItem*[mCapacity];
        std::memset(mItemList, 0, sizeof(JitterBufferItem*) * mCapacity);

        mBaseTime = now;
        mBaseRtpTimestamp = rtp_timestamp_ext;

        const auto item = newItem();
        mItemList[seq_ext & mCapacityMask] = item;
    } else if (seq_ext + mCapacity / 4 <= mMinSeq) {
        // Out of range, much less than min
        LOG(SRTC_LOG_E,
            "The new packet SEQ = %u is too late, SSRC = %u, media = %s, min = %u, max = %u",
            seq,
            mTrack->getSSRC(),
            to_string(mTrack->getMediaType()).c_str(),
            static_cast<uint16_t>(mMinSeq),
            static_cast<uint16_t>(mMaxSeq));
        return;
    } else if (seq_ext >= mMaxSeq + mCapacity / 4) {
        // Out of range, much greater than max
        LOG(SRTC_LOG_E,
            "The new packet SEQ = %u is too early, SSRC = %u, media = %s, min = %u, max = %u",
            seq,
            mTrack->getSSRC(),
            to_string(mTrack->getMediaType()).c_str(),
            static_cast<uint16_t>(mMinSeq),
            static_cast<uint16_t>(mMaxSeq));
        return;
    }

    const auto rtp_timestamp_delta = rtp_timestamp_ext - mBaseRtpTimestamp;
    const auto time_delta = std::chrono::milliseconds(1000 * rtp_timestamp_delta / mTrack->getClockRate());
    const auto packet_time = mBaseTime + time_delta;
    const auto when_dequeue = packet_time + mLength;
    const auto when_nack_request = now + mNackDelay;
    const auto when_nack_abandon = when_dequeue;

    JitterBufferItem* item = nullptr;

    if (seq_ext < mMinSeq) {
        // Before min
        if (seq_ext + mCapacity < mMaxSeq) {
            LOG(SRTC_LOG_E,
                "The new packet with SEQ = %u would exceed the capacity, SSRC = %u, media = %s, min = %u, max = %u",
                static_cast<uint16_t>(seq_ext),
                mTrack->getSSRC(),
                to_string(mTrack->getMediaType()).c_str(),
                static_cast<uint16_t>(mMinSeq),
                static_cast<uint16_t>(mMaxSeq));
            return;
        }

        for (auto seq_lost = mMinSeq - 1; seq_lost > seq_ext; seq_lost -= 1) {
            const auto item_lost = newLostItem(when_nack_request, when_nack_abandon, seq_lost);

            const auto index = item_lost->seq_ext & mCapacityMask;
            assert(mItemList[index] == nullptr);
            mItemList[index] = item_lost;

#ifdef NDEBUG
#else
            const auto diff_request = diff_millis(item_lost->when_nack_request, now);
            const auto diff_abandon = diff_millis(item_lost->when_nack_abandon, now);

            LOG(SRTC_LOG_V,
                "Storing NACK for item_lost SEQ = %u, NEW_SEQ = %u, diff_request = %d, diff_abandon = %d",
                static_cast<uint16_t>(item_lost->seq_ext),
                seq,
                diff_request,
                diff_abandon);
#endif
        }

        item = newItem();
        const auto index = seq_ext & mCapacityMask;
        assert(mItemList[index] == nullptr);
        mItemList[index] = item;

        mMinSeq = seq_ext;
    } else if (seq_ext >= mMaxSeq) {
        // Above max
        if (seq_ext > mMinSeq + mCapacity) {
            LOG(SRTC_LOG_E,
                "The new packet with SEQ = %u would exceed the capacity, SSRC = %u, media = %s, min = %u, max = %u",
                static_cast<uint16_t>(seq_ext),
                mTrack->getSSRC(),
                to_string(mTrack->getMediaType()).c_str(),
                static_cast<uint16_t>(mMinSeq),
                static_cast<uint16_t>(mMaxSeq));
            return;
        }

        for (auto seq_lost = mMaxSeq; seq_lost < seq_ext; seq_lost += 1) {
            const auto item_lost = newLostItem(when_nack_request, when_nack_abandon, seq_lost);

            const auto index = item_lost->seq_ext & mCapacityMask;
            assert(mItemList[index] == nullptr);
            mItemList[index] = item_lost;

#ifdef NDEBUG
#else
            const auto diff_request = diff_millis(item_lost->when_nack_request, now);
            const auto diff_abandon = diff_millis(item_lost->when_nack_abandon, now);

            LOG(SRTC_LOG_V,
                "Storing NACK for item_lost SEQ = %u, NEW_SEQ = %u, diff_request = %d, diff_abandon = %d",
                static_cast<uint16_t>(item_lost->seq_ext),
                seq,
                diff_request,
                diff_abandon);
#endif
        }

        item = newItem();
        const auto index = seq_ext & mCapacityMask;
        assert(mItemList[index] == nullptr);
        mItemList[index] = item;

        mMaxSeq = seq_ext + 1;
    } else {
        // Somewhere in the middle, we should already have an item there
        const auto index = seq_ext & mCapacityMask;
        item = mItemList[index];
        assert(item);
    }

    item->when_received = now;
    item->when_dequeue = when_dequeue;
    item->when_nack_request = when_nack_request;
    item->when_nack_abandon = when_dequeue;

    item->received = true;
    item->nack_needed = false;

    item->seq_ext = seq_ext;
    item->rtp_timestamp_ext = rtp_timestamp_ext;
    item->marker = packet->getMarker();

    item->payload = std::move(payload);

    item->kind = PacketKind::Standalone;
    item->kind = mDepacketizer->getPacketKind(item);
}

int JitterBuffer::getTimeoutMillis(int defaultTimeout) const
{
    if (!mItemList) {
        return defaultTimeout;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto cutoff = now + std::chrono::milliseconds(defaultTimeout);

    std::optional<int> when_request;
    std::optional<int> when_abandon;
    std::optional<int> when_dequeue;

    if (!mTrack->hasNack()) {
        when_request = 2 * defaultTimeout;
        when_abandon = 2 * defaultTimeout;
    }

    // We add packets on the Max end and consume them from the Min end
    for (auto seq = mMinSeq; seq < mMaxSeq; seq += 1) {
        const auto index = seq & mCapacityMask;
        const auto item = mItemList[index];
        assert(item);

        if (item->received) {
            // Depacketization
            if (!when_dequeue.has_value()) {
                when_dequeue = diff_millis(item->when_dequeue, now);
            }
        } else {
            // Requesting and abandoning nacks
            if (!when_request.has_value() && item->nack_needed) {
                when_request = diff_millis(item->when_nack_request, now);
            }
            if (!when_abandon.has_value()) {
                when_abandon = diff_millis(item->when_nack_abandon, now);
            }
        }

        if (when_dequeue.has_value() && when_request.has_value() && when_abandon.has_value()) {
            break;
        }
        if (item->when_dequeue > cutoff && item->when_nack_request > cutoff && item->when_nack_abandon > cutoff) {
            break;
        }
    }

    auto timeout = defaultTimeout;
    if (when_request.has_value() && when_request.value() < timeout) {
        timeout = when_request.value();
    }
    if (when_abandon.has_value() && when_abandon.value() < timeout) {
        timeout = when_abandon.value();
    }
    if (when_dequeue.has_value() && when_dequeue.value() < timeout) {
        timeout = when_dequeue.value();
    }

    return timeout;
}

std::vector<std::shared_ptr<EncodedFrame>> JitterBuffer::processDeque()
{
    std::vector<std::shared_ptr<EncodedFrame>> result;

    if (!mItemList || mMinSeq == mMaxSeq) {
        return result;
    }

    const auto now = std::chrono::steady_clock::now();

    // We add packets on the Max end and consume them from the Min end
    while (mMinSeq < mMaxSeq) {
        const auto seq = mMinSeq;
        const auto index = seq & mCapacityMask;
        const auto item = mItemList[index];
        assert(item);

        if (item->received && diff_millis(item->when_dequeue, now) <= 0) {
            if (item->kind == PacketKind::Standalone) {
                // A standalone packet, which is ready to be extracted, possibly into multiple frames
                mDepacketizer->extract(mTempFrameList, item);

                // Append to result frame list
                appendToResult(result, item, item, now, mTempFrameList);

                // Cleanup
                mItemList[index] = nullptr;
                mMinSeq += 1;

                deleteItem(item);
            } else if (item->kind == PacketKind::Start) {
                // Start of a multi-packet sequence
                uint64_t maxSeq = 0;
                if (findMultiPacketSequence(maxSeq)) {
                    // Create a list of buffers
                    extractBufferList(mTempBufferList, seq, maxSeq);

#ifdef NDEBUG
#else
                    assert(!mTempBufferList.empty());
                    assert(mDepacketizer->getPacketKind(mTempBufferList.front()) == PacketKind::Start);
                    for (size_t i = 1; i < mTempBufferList.size() - 1; i += 1) {
                        assert(mDepacketizer->getPacketKind(mTempBufferList[i]) == PacketKind::Middle);
                    }
                    assert(mDepacketizer->getPacketKind(mTempBufferList.back()) == PacketKind::End);
#endif

                    // Extract, possibly into multiple frames (theoretical)
                    mDepacketizer->extract(mTempFrameList, mTempBufferList);

                    // Append to result frame list
                    const auto maxIndex = maxSeq & mCapacityMask;
                    const auto maxItem = mItemList[maxIndex];
                    assert(maxItem);

                    appendToResult(result, item, maxItem, now, mTempFrameList);

                    // Clean up
                    deleteItemList(seq, maxSeq);

                    // Advance
                    mMinSeq = maxSeq + 1;
                } else if (findNextToDequeue(now)) {
                    // There is another frame that's ready, delete this one
                    LOG(SRTC_LOG_W,
                        "Dropping an incomplete multi-frame, SEQ = %u, MIN = %u, MAX = %u",
                        static_cast<uint16_t>(seq),
                        static_cast<uint16_t>(mMinSeq),
                        static_cast<uint16_t>(mMaxSeq));

                    mItemList[index] = nullptr;
                    mMinSeq += 1;

                    deleteItem(item);
                } else {
                    // There is no other frame, we can afford to wait longer
                    break;
                }
            } else {
                // We cannot and will never be able to extract this
                mItemList[index] = nullptr;
                mMinSeq += 1;

                deleteItem(item);
            }
        } else if (!item->received && diff_millis(item->when_nack_abandon, now) <= 0) {
            // A nack that was never received - delete and keep going
            mItemList[index] = nullptr;
            mMinSeq += 1;

            deleteItem(item);
        } else {
            break;
        }
    }

    return result;
}

std::vector<uint16_t> JitterBuffer::processNack()
{
    std::vector<uint16_t> result;

    if (!mItemList || mMinSeq == mMaxSeq || !mTrack->hasNack()) {
        return result;
    }

    const auto now = std::chrono::steady_clock::now();

    // We add packets on the Max end and consume them from the Min end
    for (auto seq = mMinSeq; seq < mMaxSeq; seq += 1) {
        const auto index = seq & mCapacityMask;
        const auto item = mItemList[index];
        assert(item);

        if (diff_millis(item->when_nack_request, now) <= 0) {
            if (!item->received && item->nack_needed) {
                item->nack_needed = false;
                result.push_back(static_cast<uint16_t>(item->seq_ext));

#ifdef NDEBUG
#else
                const auto diff_request = diff_millis(item->when_nack_request, now);
                const auto diff_abandon = diff_millis(item->when_nack_abandon, now);

                LOG(SRTC_LOG_V,
                    "Processing NACK for item SEQ = %u, diff_request = %d, diff_abandon = %d, min = %u, max = %u",
                    static_cast<uint16_t>(item->seq_ext),
                    diff_request,
                    diff_abandon,
                    static_cast<uint16_t>(mMinSeq),
                    static_cast<uint16_t>(mMaxSeq));
#endif
            }
        } else if (item->when_nack_abandon <= now) {
            break;
        }
    }

    return result;
}

void JitterBuffer::freeEverything()
{
    if (mItemList) {
        for (uint64_t seq = mMinSeq; seq < mMaxSeq; seq += 1) {
            const auto index = seq & (mCapacity - 1);
            deleteItem(mItemList[index]);
        }

        delete[] mItemList;
        mItemList = nullptr;
    }
}

JitterBufferItem* JitterBuffer::newItem()
{
    return mItemAllocator.create();
}

void JitterBuffer::deleteItem(JitterBufferItem* item)
{
    mItemAllocator.destroy(item);
}

JitterBufferItem* JitterBuffer::newLostItem(const std::chrono::steady_clock::time_point& when_nack_request,
                                            const std::chrono::steady_clock::time_point& when_nack_abandon,
                                            uint64_t seq_lost)
{
    const auto item_lost = newItem();

    item_lost->when_received = std::chrono::steady_clock::time_point::min();
    item_lost->when_dequeue = std::chrono::steady_clock::time_point::max();
    item_lost->when_nack_request = when_nack_request;
    item_lost->when_nack_abandon = when_nack_abandon;

    item_lost->received = false;
    item_lost->nack_needed = true;

    item_lost->seq_ext = seq_lost;
    item_lost->rtp_timestamp_ext = 0;

    return item_lost;
}

void JitterBuffer::extractBufferList(std::vector<const JitterBufferItem*>& out, uint64_t start, uint64_t max)
{
    out.clear();

    for (uint64_t seq = start; seq <= max; seq += 1) {
        const auto index = seq & mCapacityMask;
        auto item = mItemList[index];
        assert(item);
        out.push_back(item);
    }
}

void JitterBuffer::deleteItemList(uint64_t start, uint64_t max)
{
    for (auto seq = start; seq <= max; seq += 1) {
        const auto index = seq & mCapacityMask;
        const auto item = mItemList[index];
        assert(item);
        mItemList[index] = nullptr;
        deleteItem(item);
    }
}

void JitterBuffer::appendToResult(std::vector<std::shared_ptr<srtc::EncodedFrame>>& result,
                                  JitterBufferItem* item,
                                  JitterBufferItem* last,
                                  const std::chrono::steady_clock::time_point& now,
                                  std::vector<srtc::ByteBuffer>& list)
{
    if (!list.empty()) {
        // Check that we're not trying to go backwards
        if (mLastFrameTimeStamp.has_value() && mLastFrameTimeStamp.value() > item->rtp_timestamp_ext) {
            LOG(SRTC_LOG_W,
                "Will not de-queue %s frame with ts = %" PRIu64 ", because it's older than last frame time %" PRIu64,
                to_string(mTrack->getMediaType()).c_str(),
                item->rtp_timestamp_ext,
                mLastFrameTimeStamp.value());
            return;
        }

        mLastFrameTimeStamp = item->rtp_timestamp_ext;

        for (size_t i = 0; i < list.size(); i++) {
            const auto frame = std::make_shared<EncodedFrame>();

            frame->track = mTrack;
            frame->seq_ext = item->seq_ext;
            frame->rtp_timestamp_ext = item->rtp_timestamp_ext;
            frame->marker = last->marker && i == list.size() - 1;
            frame->first_to_last_packet_millis = diff_millis(last->when_received, item->when_received);
            frame->wait_time_millis = diff_millis(now, last->when_received);
            frame->data = std::move(list[i]);

            assert(!frame->data.empty());

            result.push_back(frame);
        }
    }
}

bool JitterBuffer::findMultiPacketSequence(uint64_t& outEnd)
{
    auto index = (mMinSeq)&mCapacityMask;
    auto item = mItemList[index];
    assert(item);
    assert(item->received);
    assert(item->kind == PacketKind::Start);

    for (auto seq = mMinSeq + 1; seq < mMaxSeq; seq += 1) {
        index = seq & mCapacityMask;
        item = mItemList[index];
        assert(item);

        if (!item->received) {
            break;
        } else if (item->kind == PacketKind::End) {
            outEnd = seq;
            return true;
        } else if (item->kind != PacketKind::Middle) {
            deleteItemList(mMinSeq, seq);
            mMinSeq = seq + 1;
            break;
        }
    }

    return false;
}

bool JitterBuffer::findNextToDequeue(const std::chrono::steady_clock::time_point& now)
{
    auto index = (mMinSeq)&mCapacityMask;
    auto item = mItemList[index];
    assert(item);
    assert(item->received);
    assert(item->kind == PacketKind::Start);

    const auto startTimestamp = item->rtp_timestamp_ext;

    for (auto seq = mMinSeq + 1; seq < mMaxSeq; seq += 1) {
        index = seq & mCapacityMask;
        item = mItemList[index];
        assert(item);

        if (item->received && item->rtp_timestamp_ext > startTimestamp) {
            if (diff_millis(item->when_dequeue, now) <= 0) {
                return true;
            }
        }
    }

    return false;
}

} // namespace srtc

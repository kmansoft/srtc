#include "srtc/jitter_buffer.h"
#include "srtc/depacketizer.h"
#include "srtc/logging.h"
#include "srtc/rtp_packet.h"
#include "srtc/srtc.h"
#include "srtc/track.h"

#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <limits>

#define LOG(level, ...) srtc::log(level, "JitterBuffer", __VA_ARGS__)

namespace
{

constexpr auto kNoPacketsResetDelay = std::chrono::milliseconds(2000);

// We calculate timeouts for the event loop (when there are no network events) with millisecond precision. When we
// time out and go to process actual logic like de-queuing frames or sending nacks, we need to use millisecond precision
// as well. If we don't, we might have a timeout = 0, but the actual logic won't run because it's 12 nanoseconds in the
// future or something. Then we'll end up cycling the event loop with 0 millisecond timeout value several times, which
// is bad for optimization.

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
            LOG(SRTC_LOG_E, "RTX payload is less than 2 bytes, can't be");
            return;
        }

        ByteReader reader(payload);
        seq = reader.readU16();

        payload = { payload.data() + 2, payload.size() - 2 };
    }

    // Extend
    const auto seq_ext = mExtValueSeq.extend(seq);
    const auto rtp_timestamp_ext = mExtValueRtpTimestamp.extend(packet->getTimestamp());

    // Decide what to do
    const auto now = std::chrono::steady_clock::now();

    if (mItemList) {
        const auto elapsed = now - mLastPacketTime;
        if (elapsed >= kNoPacketsResetDelay) {
            LOG(SRTC_LOG_E,
                "We have not had %s packets for %ld milliseconds, resetting the jitter buffer",
                to_string(mTrack->getMediaType()).c_str(),
                static_cast<long>(elapsed.count()));
            freeEverything();
        }
    }
    mLastPacketTime = now;

    if (mItemList == nullptr) {
        // First packet
        mMinSeq = seq_ext;
        mMaxSeq = mMinSeq + 1;
        mItemList = new Item*[mCapacity];
        mBaseTime = std::chrono::steady_clock::now();
        mBaseRtpTimestamp = rtp_timestamp_ext;

        const auto item = newItem();
        mItemList[seq_ext & mCapacityMask] = item;
    } else if (seq_ext + mCapacity / 4 < mMinSeq) {
        // Out of range, much less than min
        LOG(SRTC_LOG_E,
            "The new packet's sequence number %u is too late, ssrc = %u, media = %s, min = %u, max = %u",
            seq,
            mTrack->getSSRC(),
            to_string(mTrack->getMediaType()).c_str(),
            static_cast<uint16_t>(mMinSeq),
            static_cast<uint16_t>(mMaxSeq));
        return;
    } else if (seq_ext - mCapacity / 4 > mMaxSeq) {
        // Out of range, much greater than max
        LOG(SRTC_LOG_E,
            "The new packet's sequence number %u is too early, ssrc = %u, media = %s, min = %u, max = %u",
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

    Item* item = nullptr;

    if (seq_ext < mMinSeq) {
        // Before min
        if (mMaxSeq - seq_ext > mCapacity) {
            LOG(SRTC_LOG_E,
                "The new packet with sequence number %" PRIu64 " (%" PRIx64 ") would exceed the capacity",
                seq_ext,
                seq_ext);
            return;
        }

        while (seq_ext + 1 < mMinSeq) {
            mMinSeq -= 1;

            const auto lost = newItem();
            lost->when_received = std::chrono::steady_clock::time_point::min();
            lost->when_dequeue = std::chrono::steady_clock::time_point::max();
            lost->when_nack_request = when_nack_request;
            lost->when_nack_abandon = when_dequeue;

            lost->received = false;
            lost->nack_needed = true;

            lost->seq_ext = mMinSeq;
            lost->rtp_timestamp_ext = 0;

            const auto index = lost->seq_ext & mCapacityMask;
            mItemList[index] = lost;
        }

        mMinSeq -= 1;
        assert(mMinSeq == seq_ext);

        const auto index = seq_ext & mCapacityMask;
        item = newItem();
        mItemList[index] = item;
    } else if (seq_ext >= mMaxSeq) {
        // Above max
        if (seq_ext - mMinSeq > mCapacity) {
            LOG(SRTC_LOG_E,
                "The new packet with sequence number %" PRIu64 " (%" PRIx64 ") would exceed the capacity",
                seq_ext,
                seq_ext);
            return;
        }

        while (mMaxSeq <= seq_ext - 1) {
            const auto lost = newItem();
            lost->when_received = std::chrono::steady_clock::time_point::min();
            lost->when_dequeue = std::chrono::steady_clock::time_point::max();
            lost->when_nack_request = when_nack_request;
            lost->when_nack_abandon = when_dequeue;

            lost->received = false;
            lost->nack_needed = true;

            lost->seq_ext = mMaxSeq;
            lost->rtp_timestamp_ext = 0;

            const auto index = lost->seq_ext & mCapacityMask;
            mItemList[index] = lost;

            mMaxSeq += 1;
        }

        mMaxSeq += 1;
        assert(mMaxSeq - 1 == seq_ext);

        item = newItem();
        const auto index = seq_ext & mCapacityMask;
        mItemList[index] = item;
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

    item->kind = mDepacketizer->getPacketKind(payload);

    item->seq_ext = seq_ext;
    item->rtp_timestamp_ext = rtp_timestamp_ext;

    item->payload = std::move(payload);
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
        if (item->when_dequeue > cutoff && item->when_nack_abandon > cutoff) {
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
                auto frameList = mDepacketizer->extract(item->payload);

                while (!frameList.empty()) {
                    const auto frame = std::make_shared<EncodedFrame>();

                    frame->track = mTrack;
                    frame->seq_ext = item->seq_ext;
                    frame->rtp_timestamp_ext = item->rtp_timestamp_ext;
                    frame->data = std::move(frameList.front());

                    result.push_back(frame);

                    frameList.pop_front();
                }

                mItemList[index] = nullptr;
                mMinSeq += 1;

                deleteItem(item);
            } else if (item->kind == PacketKind::Start) {
                // Start of a multi-packet sequence
                uint64_t maxSeq = 0;
                if (findMultiPacketSequence(maxSeq)) {

                    std::vector<ByteBuffer*> bufList;
                    for (auto extract_seq = seq; extract_seq <= maxSeq; extract_seq += 1) {
                        const auto extract_index = extract_seq & mCapacityMask;
                        auto extract_item = mItemList[extract_index];
                        assert(extract_item);
                        bufList.push_back(&extract_item->payload);
                    }

                    // Extract, possibly into multiple frames (theoretical)
                    auto frameList = mDepacketizer->extract(bufList);

                    while (!frameList.empty()) {
                        const auto frame = std::make_shared<EncodedFrame>();

                        frame->track = mTrack;
                        frame->seq_ext = item->seq_ext;
                        frame->rtp_timestamp_ext = item->rtp_timestamp_ext;
                        frame->data = std::move(frameList.front());

                        result.push_back(frame);

                        frameList.pop_front();
                    }

                    for (auto cleanup_seq = seq; cleanup_seq <= maxSeq; cleanup_seq += 1) {
                        const auto cleanup_index = cleanup_seq & mCapacityMask;
                        deleteItem(mItemList[cleanup_index]);
                        mItemList[cleanup_index] = nullptr;
                    }

                    mMinSeq = maxSeq + 1;
                } else {
                    break;
                }
            } else {
                // We cannot extract this
                if (findNextToDequeue(now)) {
                    // There is another frame that's ready, delete this one
                    mItemList[index] = nullptr;
                    mMinSeq += 1;

                    delete item;
                } else {
                    // There is no another frame, we can afford to wait
                    break;
                }
            }
        } else if (diff_millis(item->when_nack_abandon, now) <= 0) {
            // A nack that was never received - delete and keep going
            mItemList[index] = nullptr;
            mMinSeq += 1;

            delete item;
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
            delete mItemList[index];
        }

        delete[] mItemList;
        mItemList = nullptr;
    }
}

JitterBuffer::Item* JitterBuffer::newItem()
{
    return new Item;
}

void JitterBuffer::deleteItem(Item* item)
{
    delete item;
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
            for (auto delete_seq = mMinSeq; delete_seq <= seq; delete_seq += 1) {
                const auto delete_index = delete_seq & mCapacityMask;
                const auto delete_item = mItemList[delete_index];
                deleteItem(delete_item);
                mItemList[delete_index] = nullptr;
            }

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

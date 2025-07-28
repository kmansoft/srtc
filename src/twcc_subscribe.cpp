#include "srtc/twcc_subscribe.h"
#include "srtc/logging.h"
#include "srtc/twcc_common.h"

#include <cassert>
#include <cinttypes>
#include <cstring>

#define LOG(level, ...) srtc::log(level, "TWCC", __VA_ARGS__)

namespace
{

constexpr auto kMaxPacketCount = 2048u;
constexpr auto kMaxPacketMask = kMaxPacketCount - 1;

static_assert((kMaxPacketCount & kMaxPacketMask) == 0); // Power of 2

constexpr auto kGenerateIntervalMicros = 100 * 1000;
constexpr auto kMaxPacketSize = 1000;

// Reference time is expressed as units of 64 milliseconds
constexpr auto kReferenceTimeUnits = 64 * 1000;

// Small deltas are represented by an uint8 in units of 250 micros
// Range: [0, 31.750 ms]
constexpr auto kMinSmallDeltaMicros = 0;
constexpr auto kMaxSmallDeltaMicros = 250 * std::numeric_limits<uint8_t>::max();

// Large deltas are represented by an int16 in units of 250 micros
// Range: [-8192, 8191.75 ms]
constexpr auto kMinLargeDeltaMicros = 250 * std::numeric_limits<int16_t>::min();
constexpr auto kMaxLargeDeltaMicros = 250 * std::numeric_limits<int16_t>::max();

class Builder
{
public:
    explicit Builder();
    ~Builder();

    void setBaseSeq(uint16_t seq);
    void setReferenceTime(int64_t ref_time_micros);

    void addNotReceivedRun(uint16_t count);
    void addSmallDeltaRun(uint16_t count, const bool* received, const int32_t* delta_micros);
    void addLargeDeltaRun(uint16_t count, const bool* received, const int32_t* delta_micros);

    [[nodiscard]] bool empty() const;
    [[nodiscard]] size_t size() const;

    [[nodiscard]] srtc::ByteBuffer generate(uint8_t fb_count) const;

private:
    uint16_t mBaseSeq;
    uint16_t mPacketStatusCount;
    int64_t mRefTimeMicros;

    srtc::ByteBuffer mBufHeaders;
    srtc::ByteWriter mWriterHeaders;

    srtc::ByteBuffer mBufTimestmaps;
    srtc::ByteWriter mWriterTimestamps;
};

Builder::Builder()
    : mBaseSeq(0)
    , mPacketStatusCount(0)
    , mRefTimeMicros(0)
    , mWriterHeaders(mBufHeaders)
    , mWriterTimestamps(mBufTimestmaps)
{
}

Builder::~Builder() = default;

void Builder::setBaseSeq(uint16_t seq)
{
    mBaseSeq = seq;
}

void Builder::setReferenceTime(int64_t ref_time_micros)
{
    // Ref time is represented as units of 64 millis
    assert((ref_time_micros % kReferenceTimeUnits) == 0);
    mRefTimeMicros = ref_time_micros;
}

void Builder::addNotReceivedRun(uint16_t count)
{
    // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.3
    assert(count <= 8191);

    const auto header =
        static_cast<uint16_t>((srtc::twcc::kCHUNK_RUN_LENGTH << 15) | (srtc::twcc::kSTATUS_NOT_RECEIVED << 13) | count);
    mWriterHeaders.writeU16(header);

    mPacketStatusCount += count;
}

void Builder::addSmallDeltaRun(uint16_t count, const bool* received, const int32_t* delta_micros)
{
    // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
    assert(count <= 14);

    auto header = static_cast<uint16_t>((srtc::twcc::kCHUNK_STATUS_VECTOR << 15) | (0 << 14));
    for (uint16_t i = 0; i < count; i += 1) {
        const auto symbol = received[i] ? srtc::twcc::kSTATUS_RECEIVED_SMALL_DELTA : srtc::twcc::kSTATUS_NOT_RECEIVED;
        header |= (symbol << (14 - i - 1));
    }
    mWriterHeaders.writeU16(header);

    for (uint16_t i = 0; i < count; i += 1) {
        int32_t delta = delta_micros[i];

        assert(delta >= kMinSmallDeltaMicros);
        assert(delta <= kMaxSmallDeltaMicros);

        const auto delta_encoded = static_cast<uint8_t>(delta / 250);
        mWriterTimestamps.writeU8(delta_encoded);
    }

    mPacketStatusCount += count;
}

void Builder::addLargeDeltaRun(uint16_t count, const bool* received, const int32_t* delta_micros)
{
    // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
    assert(count <= 7);

    auto header = static_cast<uint16_t>((srtc::twcc::kCHUNK_STATUS_VECTOR << 15) | (1 << 14));
    for (uint16_t i = 0; i < 2 * count; i += 2) {
        const auto symbol = received[i] ? srtc::twcc::kSTATUS_RECEIVED_LARGE_DELTA : srtc::twcc::kSTATUS_NOT_RECEIVED;
        header |= (symbol << (14 - i - 2));
    }
    mWriterHeaders.writeU16(header);

    for (uint16_t i = 0; i < count; i += 1) {
        int32_t delta = delta_micros[i];

        assert(delta >= kMinLargeDeltaMicros);
        assert(delta <= kMaxLargeDeltaMicros);

        const auto delta_encoded = static_cast<int32_t>(delta / 250);
        mWriterTimestamps.writeU16(delta_encoded);
    }

    mPacketStatusCount += count;
}

bool Builder::empty() const
{
    return mBufHeaders.empty() && mBufTimestmaps.empty();
}

size_t Builder::size() const
{
    return 2 * 3 + mBufHeaders.size() + mBufTimestmaps.size();
}

srtc::ByteBuffer Builder::generate(uint8_t fb_count) const
{
    // Message header
    srtc::ByteBuffer buf;
    srtc::ByteWriter writer(buf);

    writer.writeU16(mBaseSeq);
    writer.writeU16(mPacketStatusCount);

    const auto reference_time = mRefTimeMicros / kReferenceTimeUnits;
    const auto reference_time_and_fb_pkt_count = static_cast<uint32_t>((reference_time << 8) | fb_count);
    writer.writeU16(reference_time_and_fb_pkt_count);

    // Individual packet headers
    writer.write(mBufHeaders);

    // Timestamps
    writer.write(mBufTimestmaps);

    return buf;
}

} // namespace

namespace srtc::twcc
{

SubscribePacketHistory::SubscribePacketHistory(int64_t base_time_micros)
    : mBaseTimeMicros(base_time_micros)
    , mLastGeneratedMicros(0)
    , mPacketList(nullptr)
    , mMinSeq(0)
    , mMaxSeq(0)
    , mFbCount(0)
{
}

SubscribePacketHistory::~SubscribePacketHistory()
{
    for (auto seq = mMinSeq; seq < mMaxSeq; seq += 1) {
        const auto index = static_cast<size_t>(seq & kMaxPacketMask);
        const auto packet = mPacketList[index];
        assert(packet);
        deletePacket(packet);
        mPacketList[index] = nullptr;
    }

    delete[] mPacketList;
    mPacketList = nullptr;
}

void SubscribePacketHistory::saveIncomingPacket(uint16_t seq, int64_t now)
{
    size_t index;
    SubscribePacket* packet;

    const auto seq_ext = mExtendedValueSeq.extend(seq);
    const auto now_ext = now - mBaseTimeMicros;

    if (!mPacketList || mMinSeq == mMaxSeq) {
        if (!mPacketList) {
            mPacketList = new SubscribePacket*[kMaxPacketCount];
            std::memset(mPacketList, 0, sizeof(SubscribePacket*) * kMaxPacketCount);
        }

        mMinSeq = seq_ext;
        mMaxSeq = seq_ext + 1;

        index = static_cast<size_t>(seq_ext & kMaxPacketMask);

        packet = newPacket();
        packet->seq_ext = seq_ext;
        packet->received_time_micros = now_ext;

        mPacketList[index] = packet;

        return;
    }

    if (seq_ext + kMaxPacketCount / 4 < mMinSeq) {
        LOG(SRTC_LOG_W,
            "Received seq = %u is too small, min = %u, max = %u",
            seq,
            static_cast<uint16_t>(mMinSeq),
            static_cast<uint16_t>(mMaxSeq));
        return;
    }

    if (seq_ext > mMaxSeq + kMaxPacketCount / 4) {
        LOG(SRTC_LOG_W,
            "Received seq = %u is too large, min = %u, max = %u",
            seq,
            static_cast<uint16_t>(mMinSeq),
            static_cast<uint16_t>(mMaxSeq));
        return;
    }

    if (seq_ext >= mMaxSeq) {
        // Larger than max, extend
        while (seq_ext > mMaxSeq) {
            if (mMaxSeq - mMinSeq == kMaxPacketCount) {
                deleteMinPacket();
            }

            index = static_cast<size_t>(mMaxSeq & kMaxPacketMask);

            packet = newPacket();

            packet->seq_ext = mMaxSeq;
            packet->received_time_micros = 0;

            assert(mPacketList[index] == nullptr);
            mPacketList[index] = packet;

            mMaxSeq += 1;
        }

        assert(mMaxSeq == seq_ext);

        if (mMaxSeq - mMinSeq == kMaxPacketCount) {
            deleteMinPacket();
        }

        packet = newPacket();

        packet->seq_ext = seq_ext;
        packet->received_time_micros = now_ext;

        index = static_cast<size_t>(seq_ext & kMaxPacketMask);
        assert(mPacketList[index] == nullptr);
        mPacketList[index] = packet;

        mMaxSeq += 1;
        assert(mMaxSeq - mMinSeq <= kMaxPacketCount);
    } else if (seq_ext < mMinSeq) {
        // Smaller than min, extend but only if we have capacity
        if (mMaxSeq - seq_ext > kMaxPacketCount) {
            LOG(SRTC_LOG_W,
                "Received seq = %u would overflow, min = %u, max = %u",
                seq,
                static_cast<uint16_t>(mMinSeq),
                static_cast<uint16_t>(mMaxSeq));
            return;
        }

        while (seq_ext + 1 < mMinSeq) {
            mMinSeq -= 1;

            index = static_cast<size_t>(mMinSeq & kMaxPacketMask);

            packet = newPacket();

            packet->seq_ext = mMinSeq;
            packet->received_time_micros = 0;

            assert(mPacketList[index] == nullptr);
            mPacketList[index] = packet;
        }

        assert(seq_ext + 1 == mMinSeq);

        packet = newPacket();

        packet->seq_ext = seq_ext;
        packet->received_time_micros = now_ext;

        index = static_cast<size_t>(seq_ext & kMaxPacketMask);
        assert(mPacketList[index] == nullptr);
        mPacketList[index] = packet;

        mMinSeq -= 1;
        assert(mMaxSeq - mMinSeq <= kMaxPacketCount);
    } else {
        // Somewhere in the middle
        index = static_cast<size_t>(seq_ext & kMaxPacketMask);
        packet = mPacketList[index];

        assert(packet);

        packet->received_time_micros = now_ext;
    }
}

bool SubscribePacketHistory::isTimeToGenerate(int64_t now_micros) const
{
    return now_micros - mLastGeneratedMicros >= kGenerateIntervalMicros;
}

std::list<ByteBuffer> SubscribePacketHistory::generate(int64_t now_micros)
{
#ifdef NDEBUG
#else
    for (uint64_t seq = mMinSeq; seq < mMaxSeq; seq += 1) {
        const auto index = static_cast<size_t>(mMinSeq & kMaxPacketMask);
        const auto packet = mPacketList[index];
        assert(packet);
    }
#endif

    std::list<ByteBuffer> list;

    uint16_t count = 0;
    int64_t curr_time = 0;
    auto builder = std::make_unique<Builder>();

    bool received[14] = {};
    int32_t delta_micros[14] = {};

    uint64_t total_count = mMaxSeq - mMinSeq;

    while (mMinSeq < mMaxSeq) {
        size_t index;
        SubscribePacket* packet;

        index = static_cast<size_t>(mMinSeq & kMaxPacketMask);
        packet = mPacketList[index];

        if (packet->received_time_micros == 0) {
            // Not received
            count = peekNotReceivedRun();
            assert(count >= 1 && count <= 8191);

            builder->addNotReceivedRun(count);
            advance(count);
        } else {
            if (curr_time == 0) {
                curr_time = kReferenceTimeUnits * (packet->received_time_micros / kReferenceTimeUnits);
                builder->setReferenceTime(curr_time);
            }

            if ((count = peekSmallDeltaRun(curr_time, received, delta_micros)) > 0) {
                // A run of small deltas and some possibly not received
                builder->addSmallDeltaRun(count, received, delta_micros);
                advance(count);
            } else if ((count = peekLargeDeltaRun(curr_time, received, delta_micros)) > 0) {
                // A run of large deltas and some possibly not received
                builder->addLargeDeltaRun(count, received, delta_micros);
                advance(count);
            } else {
                // The delta is too large even for the large delta encoding (unlikely but can happen), we have to start
                // a new packet which will use the new time as its reference time
                if (!builder->empty()) {
                    list.push_back(builder->generate(mFbCount++));
                    builder = std::make_unique<Builder>();
                }
                curr_time = 0;
            }
        }

        if (builder->size() >= kMaxPacketSize) {
            // Packet size has gotten too large, flush
            list.push_back(builder->generate(mFbCount++));
            builder = std::make_unique<Builder>();
            curr_time = 0;
        }
    }

    if (!builder->empty()) {
        list.push_back(builder->generate(mFbCount++));
    }

    mLastGeneratedMicros = now_micros;

#ifdef NDEBUG
    (void)total_count;
#else
    if (total_count > 0) {
        std::printf("TWCC generated for %" PRIu64 " incoming packets, RTCP packet count = %zu, min = %u, max = %u\n",
                    total_count,
                    list.size(),
                    static_cast<uint16_t>(mMinSeq),
                    static_cast<uint16_t>(mMaxSeq));
    }
#endif

    return list;
}

void SubscribePacketHistory::deleteMinPacket()
{
    std::printf("TWCC deleting min = %u\n", static_cast<uint16_t>(mMinSeq));

    assert(mMinSeq < mMaxSeq);

    const auto index = static_cast<size_t>(mMinSeq & kMaxPacketMask);
    const auto packet = mPacketList[index];
    assert(packet);
    deletePacket(packet);
    mPacketList[index] = nullptr;
    mMinSeq += 1;
}

void SubscribePacketHistory::advance(uint64_t count)
{
    std::printf("TWCC deleting %u packets, min = %u\n", static_cast<uint16_t>(count), static_cast<uint16_t>(mMinSeq));

    for (uint64_t i = 0; i < count; i += 1) {
        assert(mMinSeq < mMaxSeq);

        const auto index = static_cast<size_t>(mMinSeq & kMaxPacketMask);
        const auto packet = mPacketList[index];
        assert(packet);
        deletePacket(packet);
        mPacketList[index] = nullptr;
        mMinSeq += 1;
    }
}

SubscribePacket* SubscribePacketHistory::newPacket()
{
    return mPacketAllocator.create();
}

void SubscribePacketHistory::deletePacket(SubscribePacket* packet)
{
    mPacketAllocator.destroy(packet);
}

uint16_t SubscribePacketHistory::peekNotReceivedRun() const
{
    uint64_t seq = mMinSeq;
    uint64_t max = std::min(seq + 8192, mMaxSeq);

    while (seq < max) {
        const auto index = static_cast<size_t>(seq & kMaxPacketMask);
        const auto packet = mPacketList[index];

        assert(packet);

        if (packet->received_time_micros != 0) {
            break;
        }

        seq += 1;
    }

    return static_cast<uint16_t>(seq - mMinSeq);
}

uint16_t SubscribePacketHistory::peekSmallDeltaRun(int64_t& curr_time, bool* received, int32_t* delta_micros) const
{
    uint64_t seq = mMinSeq;
    uint64_t max = std::min(seq + 14, mMaxSeq);

    while (seq < max) {
        const auto index = static_cast<size_t>(seq & kMaxPacketMask);
        const auto packet = mPacketList[index];

        assert(packet);

        const auto out = seq - mMinSeq;

        if (packet->received_time_micros == 0) {
            // Not received
            received[out] = false;
            delta_micros[out] = 0;
        } else if (packet->received_time_micros >= curr_time + kMinSmallDeltaMicros &&
                   packet->received_time_micros <= curr_time + kMaxSmallDeltaMicros) {
            // Acceptable small delta
            received[out] = true;
            delta_micros[out] = static_cast<int32_t>(packet->received_time_micros - curr_time);
            curr_time = packet->received_time_micros;
        } else {
            break;
        }

        seq += 1;
    }

    return static_cast<uint16_t>(seq - mMinSeq);
}

uint16_t SubscribePacketHistory::peekLargeDeltaRun(int64_t& curr_time, bool* received, int32_t* delta_micros) const
{
    uint64_t seq = mMinSeq;
    uint64_t max = std::min(seq + 7, mMaxSeq);

    while (seq < max) {
        const auto index = static_cast<size_t>(seq & kMaxPacketMask);
        const auto packet = mPacketList[index];

        assert(packet);

        const auto out = seq - mMinSeq;

        if (packet->received_time_micros == 0) {
            // Not received
            received[out] = false;
            delta_micros[out] = 0;
        } else if (packet->received_time_micros >= curr_time + kMinLargeDeltaMicros &&
                   packet->received_time_micros <= curr_time + kMaxLargeDeltaMicros) {
            // Acceptable large delta
            received[out] = true;
            delta_micros[out] = static_cast<int32_t>(packet->received_time_micros - curr_time);
            curr_time = packet->received_time_micros;
        } else {
            break;
        }

        seq += 1;
    }

    return static_cast<uint16_t>(seq - mMinSeq);
}

} // namespace srtc::twcc
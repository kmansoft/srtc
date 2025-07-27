#include "srtc/twcc_subscribe.h"
#include "srtc/logging.h"

#include <cassert>
#include <cstring>

#define LOG(level, ...) srtc::log(level, "TWCC", __VA_ARGS__)

namespace
{

constexpr auto kMaxPacketCount = 2048u;
constexpr auto kMaxPacketMask = kMaxPacketCount - 1;

static_assert((kMaxPacketCount & kMaxPacketMask) == 0); // Power of 2

} // namespace

namespace srtc::twcc
{

SubscribePacketHistory::SubscribePacketHistory(int64_t base_time_micros)
    : mBaseTimeMicros(base_time_micros)
    , mPacketList(nullptr)
    , mMinSeq(0)
    , mMaxSeq(0)
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

    std::printf("TWCC received seq = %u, min = %u, max = %u\n",
                seq,
                static_cast<uint16_t>(mMinSeq),
                static_cast<uint16_t>(mMaxSeq));

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
        packet->reported_count = 0;

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
            packet->reported_count = 0;

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
        packet->reported_count = 0;

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
            packet->reported_count = 0;

            assert(mPacketList[index] == nullptr);
            mPacketList[index] = packet;
        }

        assert(seq_ext + 1 == mMinSeq);

        packet = newPacket();

        packet->seq_ext = seq_ext;
        packet->received_time_micros = now_ext;
        packet->reported_count = 0;

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

        packet->received_time_micros = now;
        packet->reported_count = 0;
    }
}

void SubscribePacketHistory::deleteMinPacket()
{
    std::printf("TWCC deleting min = %u\n", static_cast<uint16_t>(mMinSeq));

    assert(mMaxSeq - mMinSeq == kMaxPacketCount);

    const auto index = static_cast<size_t>(mMinSeq & kMaxPacketMask);
    const auto packet = mPacketList[index];
    assert(packet);
    deletePacket(packet);
    mPacketList[index] = nullptr;
    mMinSeq += 1;
}

SubscribePacket* SubscribePacketHistory::newPacket()
{
    return mPacketAllocator.create();
}

void SubscribePacketHistory::deletePacket(SubscribePacket* packet)
{
    mPacketAllocator.destroy(packet);
}

} // namespace srtc::twcc
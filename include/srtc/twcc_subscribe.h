#pragma once

#include "srtc/extended_value.h"
#include "srtc/pool_allocator.h"
#include "srtc/srtc.h"
#include "srtc/twcc_common.h"
#include "srtc/util.h"

#include <cstdint>
#include <list>

namespace srtc::twcc
{
// Status of a single subscribed RTP packet

struct SubscribePacket {
    uint64_t seq_ext;
    int64_t received_time_micros; // or 0
};

// A history of such packets

class SubscribePacketHistory
{
public:
    explicit SubscribePacketHistory(int64_t base_time_micros);
    ~SubscribePacketHistory();

    void saveIncomingPacket(uint16_t seq, int64_t now_micros);

    [[nodiscard]] bool isTimeToGenerate(int64_t now_micros) const;
    [[nodiscard]] std::list<ByteBuffer> generate(int64_t now_micros);

private:
    void deleteMinPacket();
    void advance(uint64_t count);

    SubscribePacket* newPacket();
    void deletePacket(SubscribePacket* packet);

    [[nodiscard]] uint16_t peekNotReceivedRun() const;
    [[nodiscard]] uint16_t peekSmallDeltaRun(int64_t& curr_time, bool* received, int32_t* delta_micros_list) const;
    [[nodiscard]] uint16_t peekLargeDeltaRun(int64_t& curr_time, bool* received, int32_t* delta_micros_list) const;

    const int64_t mBaseTimeMicros;
    int64_t mLastGeneratedMicros;

    ExtendedValue<uint16_t> mExtendedValueSeq;
    PoolAllocator<SubscribePacket> mPacketAllocator;

    SubscribePacket** mPacketList;
    uint64_t mMinSeq;
    uint64_t mMaxSeq; // Open interval: [mMinSeq, mMaxSeq)

    uint8_t mFbCount;
};

} // namespace srtc::twcc

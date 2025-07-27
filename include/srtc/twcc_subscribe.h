#pragma once

#include "srtc/extended_value.h"
#include "srtc/pool_allocator.h"
#include "srtc/srtc.h"
#include "srtc/twcc_common.h"
#include "srtc/util.h"

#include <cstdint>

namespace srtc::twcc
{
// Status of a single subscribed RTP packet

struct SubscribePacket {
    uint64_t seq_ext;
    int64_t received_time_micros;   // or 0

    uint8_t reported_count;
};

// A history of such packets

class SubscribePacketHistory
{
public:
    explicit SubscribePacketHistory(int64_t base_time_micros);
    ~SubscribePacketHistory();

    void saveIncomingPacket(uint16_t seq, int64_t now);

private:
    void deleteMinPacket();

    SubscribePacket* newPacket();
    void deletePacket(SubscribePacket* packet);

    const int64_t mBaseTimeMicros;

    ExtendedValue<uint16_t> mExtendedValueSeq;
    PoolAllocator<SubscribePacket> mPacketAllocator;

    SubscribePacket** mPacketList;
    uint64_t mMinSeq;
    uint64_t mMaxSeq; // Open interval: [mMinSeq, mMaxSeq)
};

} // namespace srtc::twcc

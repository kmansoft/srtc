#pragma once

#include <chrono>
#include <cstdint>

#include "srtc/srtc.h"
#include "srtc/byte_buffer.h"

namespace srtc
{

enum class PacketKind {
    Standalone = 0,
    Start = 1,
    Middle = 2,
    End = 3
};

struct JitterBufferItem {
    std::chrono::steady_clock::time_point when_received;
    std::chrono::steady_clock::time_point when_dequeue;
    std::chrono::steady_clock::time_point when_nack_request;
    std::chrono::steady_clock::time_point when_nack_abandon;

    bool received = false;
    bool nack_needed = false;

    PacketKind kind = PacketKind::Standalone;

    uint64_t seq_ext = 0;
    uint64_t rtp_timestamp_ext = 0; // only when received
    bool marker = false;            // same

    ByteBuffer payload;
};

}

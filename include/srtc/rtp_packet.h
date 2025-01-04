#pragma once

#include "srtc/byte_buffer.h"

#include <cstddef>
#include <cstdint>

namespace srtc {

class RtpPacket {
public:
    // TODO - MTU discovery
    static constexpr size_t kMaxSize = 1200;

    RtpPacket(bool marker,
              uint8_t payloadType,
              uint16_t sequence,
              uint32_t timestamp,
              uint32_t ssrc,
              ByteBuffer& payload);

    ~RtpPacket();

    [[nodiscard]] ByteBuffer generate() const;

private:
    const bool mMarker;
    const uint8_t mPayloadType;
    const uint16_t mSequence;
    const uint32_t mTimestamp;
    const uint32_t mSSRC;
    const ByteBuffer mPayload;
};

}

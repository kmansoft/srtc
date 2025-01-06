#pragma once

#include "srtc/byte_buffer.h"

#include <cstddef>
#include <cstdint>

namespace srtc {

class RtpPacket {
public:
    // https://stackoverflow.com/questions/47635545/why-webrtc-chose-rtp-max-packet-size-to-1200-bytes
    // https://webrtc.googlesource.com/src/+/refs/heads/main/media/base/media_constants.cc#17
    static constexpr size_t kMaxPayloadSize = 1200;

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

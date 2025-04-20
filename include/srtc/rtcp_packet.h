#pragma once

#include "srtc/byte_buffer.h"

#include <memory>
#include <cstddef>
#include <cstdint>

namespace srtc {

class Track;

class RtcpPacket {
public:
    RtcpPacket(const std::shared_ptr<Track>& track,
               uint8_t rc,
               uint8_t payloadId,
               ByteBuffer&& payload);

    ~RtcpPacket();

    [[nodiscard]] std::shared_ptr<Track> getTrack() const;
    [[nodiscard]] uint8_t getRC() const;
    [[nodiscard]] uint8_t getPayloadId() const;
    [[nodiscard]] uint32_t getSSRC() const;

    [[nodiscard]] ByteBuffer generate() const;

private:
    const std::shared_ptr<Track> mTrack;
    const uint32_t mSSRC;
    const uint8_t mRC;
    const uint8_t mPayloadId;
    const ByteBuffer mPayload;
};

}

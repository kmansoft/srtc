#pragma once

#include "srtc/byte_buffer.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace srtc
{

class Track;

class RtcpPacket
{
public:
	static constexpr uint8_t kSenderReport = 200;

    RtcpPacket(const std::shared_ptr<Track>& track, uint8_t rc, uint8_t payloadId, ByteBuffer&& payload);

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

} // namespace srtc

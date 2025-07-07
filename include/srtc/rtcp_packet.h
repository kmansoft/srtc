#pragma once

#include "srtc/byte_buffer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <list>

namespace srtc
{

class Track;
class ByteBuffer;

class RtcpPacket
{
public:
	static constexpr uint8_t kSenderReport = 200;
	static constexpr uint8_t kReceiverReport = 201;
	static constexpr uint8_t kFeedback = 205;
	static constexpr uint8_t kPayloadSpecific = 206;

	RtcpPacket(uint32_t ssrc, uint8_t rc, uint8_t payloadId, ByteBuffer&& payload);

    ~RtcpPacket();

    [[nodiscard]] uint32_t getSSRC() const;
    [[nodiscard]] uint8_t getRC() const;
    [[nodiscard]] uint8_t getPayloadId() const;
	[[nodiscard]] const ByteBuffer& getPayload() const;

    [[nodiscard]] ByteBuffer generate() const;

    static std::list<std::shared_ptr<RtcpPacket>> fromUdpPacket(const srtc::ByteBuffer& data);

private:
    const uint32_t mSSRC;
    const uint8_t mRC;
    const uint8_t mPayloadId;
    const ByteBuffer mPayload;
};

} // namespace srtc

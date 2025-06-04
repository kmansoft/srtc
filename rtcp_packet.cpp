#include "srtc/rtcp_packet.h"
#include "srtc/track.h"

namespace srtc
{

RtcpPacket::RtcpPacket(uint32_t ssrc, uint8_t rc, uint8_t payloadId, ByteBuffer&& payload)
	: mSSRC(ssrc)
	, mRC(rc)
	, mPayloadId(payloadId)
	, mPayload(std::move(payload))
{
}

RtcpPacket::~RtcpPacket() = default;

uint32_t RtcpPacket::getSSRC() const
{
	return mSSRC;
}

uint8_t RtcpPacket::getRC() const
{
	return mRC;
}

uint8_t RtcpPacket::getPayloadId() const
{
	return mPayloadId;
}

const ByteBuffer& RtcpPacket::getPayload() const
{
	return mPayload;
}

ByteBuffer RtcpPacket::generate() const
{
	ByteBuffer buf;
	ByteWriter w(buf);

	// https://en.wikipedia.org/wiki/RTP_Control_Protocol

	const uint16_t header = (2 << 14) | ((mRC & 0x1F) << 8) | (mPayloadId);

	const auto lenRaw = 2 + 2 + 4 + mPayload.size();
	const auto lenRTCP = (lenRaw + 3) / 4 - 1;

	w.writeU16(header);
	w.writeU16(lenRTCP);
	w.writeU32(mSSRC);

	w.write(mPayload);

	for (auto padding = lenRaw; padding % 4 != 0; padding += 1) {
		w.writeU8(0);
	}

	return buf;
}

std::list<std::shared_ptr<RtcpPacket>> RtcpPacket::fromUdpPacket(const srtc::ByteBuffer& data)
{
	std::list<std::shared_ptr<RtcpPacket>> list;

	auto ptr = data.data();
	const auto end = ptr + data.size();

	while (ptr + 8 <= end) {
		// Header
		const auto padding = static_cast<uint8_t>(ptr[0] & 0x20);
		const auto rc = static_cast<uint8_t>(ptr[0] & 0x1f);
		const auto payloadId = ptr[1];
		const auto len4 = (ptr[2] << 8) | ptr[3];
		const auto len = (len4 + 1) * 4;
		const auto ssrc = static_cast<uint32_t>(ptr[4]) << 24 | static_cast<uint32_t>(ptr[5]) << 16 |
						  static_cast<uint32_t>(ptr[6]) << 8 | static_cast<uint32_t>(ptr[7]);

		if (ptr + len > end) {
			break;
		}

		// Payload
		ByteBuffer payload(ptr + 8, len - 8);

		// Remove padding if any
		if (padding && !payload.empty()) {
			const auto amount = payload.data()[len - 8 - 1];
			if (amount <= payload.size()) {
				payload.resize(payload.size() - amount);
			}
		}

		list.push_back(std::make_shared<RtcpPacket>(ssrc, rc, payloadId, std::move(payload)));

		// Move to the next packet
		ptr += len;
	}

	return list;
}

} // namespace srtc
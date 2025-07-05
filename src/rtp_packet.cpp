#include "srtc/rtp_packet.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/track.h"

#include <cassert>
#include <utility>

namespace
{

void writeExtension(srtc::ByteWriter& writer, const srtc::RtpExtension& extension)
{
	if (extension.empty()) {
		return;
	}

	// https://datatracker.ietf.org/doc/html/rfc3550#section-5.3.1
	writer.writeU16(extension.getId());

	const auto& extensionData = extension.getData();
	const auto extensionSize = extensionData.size();
	writer.writeU16((extensionSize + 3) / 4);

	writer.write(extensionData);

	for (size_t padding = extensionSize; padding % 4 != 0; padding += 1) {
		writer.writeU8(0);
	}
}

void writePayload(srtc::ByteWriter& writer, const srtc::ByteBuffer& payload)
{
	writer.write(payload);
}

void writePadding(srtc::ByteWriter& writer, uint8_t padding)
{
	if (padding == 0) {
		return;
	}

	writer.padding(0, padding - 1);
	writer.writeU8(padding);
}

} // namespace

namespace srtc
{

RtpPacket::RtpPacket(const std::shared_ptr<Track>& track,
					 bool marker,
					 uint32_t rollover,
					 uint16_t sequence,
					 uint32_t timestamp,
					 uint8_t padding,
					 ByteBuffer&& payload)
	: mTrack(track)
	, mSSRC(track->getSSRC())
	, mPayloadId(track->getPayloadId())
	, mMarker(marker)
	, mRollover(rollover)
	, mSequence(sequence)
	, mTimestamp(timestamp)
	, mPaddingSize(padding)
	, mPayload(std::move(payload))
{
}

RtpPacket::RtpPacket(const std::shared_ptr<Track>& track,
					 bool marker,
					 uint32_t rollover,
					 uint16_t sequence,
					 uint32_t timestamp,
					 uint8_t padding,
					 RtpExtension&& extension,
					 ByteBuffer&& payload)
	: mTrack(track)
	, mSSRC(track->getSSRC())
	, mPayloadId(track->getPayloadId())
	, mMarker(marker)
	, mRollover(rollover)
	, mSequence(sequence)
	, mTimestamp(timestamp)
	, mPaddingSize(padding)
	, mPayload(std::move(payload))
	, mExtension(std::move(extension))
{
}

RtpPacket::~RtpPacket() = default;

std::shared_ptr<Track> RtpPacket::getTrack() const
{
	return mTrack;
}

const RtpExtension& RtpPacket::getExtension() const
{
	return mExtension;
}

bool RtpPacket::getMarker() const
{
	return mMarker;
}

uint8_t RtpPacket::getPayloadId() const
{
	return mPayloadId;
}

uint32_t RtpPacket::getRollover() const
{
	return mRollover;
}

uint8_t RtpPacket::getPaddingSize() const
{
	return mPaddingSize;
}

size_t RtpPacket::getPayloadSize() const
{
	return mPayload.size();
}

uint16_t RtpPacket::getSequence() const
{
	return mSequence;
}

RtpPacket::Output RtpPacket::generate() const
{
	// https://blog.webex.com/engineering/introducing-rtp-the-packet-format/

	ByteBuffer buf;
	ByteWriter writer(buf);

	// V=2 | P | X | CC | M | PT
	const auto pad = mPaddingSize != 0;
	const auto ext = !mExtension.empty();
	const uint16_t header = (2 << 14) | (pad ? (1 << 13) : 0) | (ext ? (1 << 12) : 0) | (mMarker ? (1 << 7) : 0) | (mPayloadId & 0x7Fu);
	writer.writeU16(header);

	writer.writeU16(mSequence);
	writer.writeU32(mTimestamp);
	writer.writeU32(mSSRC);

	// Extension
	writeExtension(writer, mExtension);

	// Payload
	writePayload(writer, mPayload);

	// Write padding
	writePadding(writer, mPaddingSize);

#if 0
	if (mPaddingSize > 0) {
		std::printf("***** Packet with padding: payload size = %zu, ext size = %zu, "
					"padding size = %u, total size = %zu, last byte of payload = %u\n",
					mPayload.size(),
					mExtension.size(),
					mPaddingSize,
					buf.size(),
					mPayload.data()[mPayload.size() - 1]);
	}
#endif

	return { std::move(buf), mRollover };
}

uint32_t RtpPacket::getSSRC() const
{
	return mSSRC;
}

uint32_t RtpPacket::getTimestamp() const
{
	return mTimestamp;
}

const ByteBuffer& RtpPacket::getPayload() const
{
	return mPayload;
}

ByteBuffer&& RtpPacket::movePayload()
{
	return std::move(mPayload);
}

void RtpPacket::setExtension(RtpExtension&& extension)
{
	mExtension = std::move(extension);
}

RtpPacket::Output RtpPacket::generateRtx(const RtpExtension& extension) const
{
	const auto rtxPayloadId = mTrack->getRtxPayloadId();
	assert(rtxPayloadId > 0);

	// https://datatracker.ietf.org/doc/html/rfc4588#section-4

	ByteBuffer buf;
	ByteWriter writer(buf);

	// V=2 | P | X | CC | M | PT
	const auto pad = mPaddingSize != 0;
	const auto ext = !extension.empty();
	const uint16_t header = (2 << 14) | (pad ? (1 << 13) : 0) | (ext ? (1 << 12) : 0) | (mMarker ? (1 << 7) : 0) | (rtxPayloadId & 0x7Fu);
	writer.writeU16(header);

	const auto packetSource = mTrack->getRtxPacketSource();
	const auto [rtxRollover, rtxSequence] = packetSource->getNextSequence();
	writer.writeU16(rtxSequence);
	writer.writeU32(mTimestamp);
	writer.writeU32(mTrack->getRtxSSRC());

	// Extension
	writeExtension(writer, extension);

	// The original sequence
	writer.writeU16(mSequence);

	// Payload
	writePayload(writer, mPayload);

	// Padding
	writePadding(writer, mPaddingSize);

	return { std::move(buf), rtxRollover };
}

std::shared_ptr<RtpPacket> RtpPacket::fromUdpPacket(const std::shared_ptr<Track>& track, const srtc::ByteBuffer& data)
{
	ByteReader reader(data);

	if (reader.remaining() < 4 + 4 + 4) {
		return {};
	}

	const auto header = reader.readU16();
	const auto padding = (header & (1 << 13)) != 0;
	const auto marker = (header & (1 << 7)) != 0;
	const auto payloadId = header & 0x7Fu;

	const auto sequence = reader.readU16();
	const auto timestamp = reader.readU32();
	const auto ssrc = reader.readU32();

	assert((ssrc == track->getSSRC() && payloadId == track->getPayloadId()) ||
		   (ssrc == track->getRtxSSRC() && payloadId == track->getRtxPayloadId()));

	RtpExtension extension;

	if ((header & (1 << 12)) != 0) {
		if (reader.remaining() < 4) {
			return {};
		}

		const auto extId = reader.readU16();
		const auto extSize = reader.readU16() * 4u;

		if (reader.remaining() < extSize) {
			return {};
		}

		auto extData = reader.readByteBuffer(extSize);

		if (extId == RtpExtension::kOneByte && !extData.empty()) {
			extData = RtpExtension::convertOneToTwoByte(extData);
		}

		ByteReader extReader(extData);
		while (extReader.remaining() > 0) {
			const auto extId2 = extReader.readU8();
			if (extId2 == 0) {
				extData.resize(extReader.position() - 1);
				break;
			}

			if (extReader.remaining() < 1) {
				break;
			}
			const auto extLen2 = extReader.readU8();
			if (extReader.remaining() < extLen2) {
				break;
			}
			extReader.skip(extLen2);
		}

		extension = RtpExtension(extId, std::move(extData));
	}

	auto payloadSize = reader.remaining();
	auto payload = reader.readByteBuffer(payloadSize);

	if (padding) {
		if (payload.empty()) {
			return {};
		}
		const auto paddingCount = payload.data()[payloadSize - 1];
		if (paddingCount > payloadSize) {
			return {};
		}

		payloadSize -= paddingCount;
		payload.resize(payloadSize);
	}

	return std::make_shared<RtpPacket>(track, marker, 0, sequence, timestamp, 0, std::move(extension), std::move(payload));
}

} // namespace srtc

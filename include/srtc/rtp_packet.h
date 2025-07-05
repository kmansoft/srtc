#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/rtp_extension.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace srtc
{

class Track;

class RtpPacket
{
public:
	// https://stackoverflow.com/questions/47635545/why-webrtc-chose-rtp-max-packet-size-to-1200-bytes
	// https://webrtc.googlesource.com/src/+/refs/heads/main/media/base/media_constants.cc#17
	static constexpr size_t kMaxPayloadSize = 1200;

	// https://blog.webex.com/engineering/introducing-rtp-the-packet-format/
	static constexpr size_t kHeaderSize = 12;

	RtpPacket(const std::shared_ptr<Track>& track,
			  bool marker,
			  uint32_t rollover,
			  uint16_t sequence,
			  uint32_t timestamp,
			  uint8_t padding,
			  ByteBuffer&& payload);

	RtpPacket(const std::shared_ptr<Track>& track,
			  bool marker,
			  uint32_t rollover,
			  uint16_t sequence,
			  uint32_t timestamp,
			  uint8_t padding,
			  RtpExtension&& extension,
			  ByteBuffer&& payload);

	~RtpPacket();

	[[nodiscard]] std::shared_ptr<Track> getTrack() const;
	[[nodiscard]] const RtpExtension& getExtension() const;
	[[nodiscard]] bool getMarker() const;
	[[nodiscard]] uint8_t getPayloadId() const;
	[[nodiscard]] uint32_t getRollover() const;
	[[nodiscard]] uint8_t getPaddingSize() const;
	[[nodiscard]] size_t getPayloadSize() const;
	[[nodiscard]] uint16_t getSequence() const;
	[[nodiscard]] uint32_t getSSRC() const;
	[[nodiscard]] uint32_t getTimestamp() const;
	[[nodiscard]] const ByteBuffer& getPayload() const;
	[[nodiscard]] ByteBuffer&& movePayload();

	// The extension is mutable
	void setExtension(RtpExtension&& extension);

	struct Output {
		ByteBuffer buf;
		uint32_t rollover;
	};

	[[nodiscard]] Output generate() const;
	[[nodiscard]] Output generateRtx(const RtpExtension& extension) const;

	static std::shared_ptr<RtpPacket> fromUdpPacket(const std::shared_ptr<Track>& track,
													const srtc::ByteBuffer& data);

private:
	const std::shared_ptr<Track> mTrack;
	const uint32_t mSSRC;
	const uint8_t mPayloadId;
	const bool mMarker;
	const uint32_t mRollover;
	const uint16_t mSequence;
	const uint32_t mTimestamp;
	const uint8_t mPaddingSize;
	ByteBuffer mPayload;
	RtpExtension mExtension;
};

} // namespace srtc

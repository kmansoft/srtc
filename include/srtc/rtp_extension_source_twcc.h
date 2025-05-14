#pragma once

#include "srtc/rtp_extension_source.h"
#include "srtc/util.h"

#include <cstdint>
#include <memory>

namespace srtc::twcc
{
class PacketStatusHistory;
class FeedbackHeaderHistory;
}; // namespace srtc::twcc

namespace srtc
{

class Track;
class ByteBuffer;
class ByteReader;
class Packetizer;
class RtpExtensionBuilder;
class SdpOffer;
class SdpAnswer;
class RtpPacket;

class RtpExtensionSourceTWCC : public RtpExtensionSource
{
public:
	RtpExtensionSourceTWCC(uint8_t nVideoExtTWCC, uint8_t nAudioExtTWCC);
	~RtpExtensionSourceTWCC() override;

	static std::shared_ptr<RtpExtensionSourceTWCC> factory(const std::shared_ptr<SdpOffer>& offer,
														   const std::shared_ptr<SdpAnswer>& answer);

	[[nodiscard]] bool wants(const std::shared_ptr<Track>& track, bool isKeyFrame, int packetNumber) override;

	void add(RtpExtensionBuilder& builder,
			 const std::shared_ptr<Track>& track,
			 bool isKeyFrame,
			 int packetNumber) override;

	void onBeforeSendingRtpPacket(const std::shared_ptr<RtpPacket>& packet, size_t dataSize);
	void onPacketWasNacked(const std::shared_ptr<RtpPacket>& packet);

	void onReceivedRtcpPacket(uint32_t ssrc, ByteReader& reader);

	[[nodiscard]] bool getFeedbackSeq(const std::shared_ptr<RtpPacket>& packet, uint16_t& outSeq) const;

	[[nodiscard]] float getPacketsLostPercent() const;

private:
	const uint8_t mVideoExtTWCC;
	const uint8_t mAudioExtTWCC;
	uint16_t mNextPacketSEQ;
	std::shared_ptr<twcc::PacketStatusHistory> mPacketHistory;
	std::shared_ptr<twcc::FeedbackHeaderHistory> mHeaderHistory;

	struct TempPacket {
		int32_t delta_micros;
		uint8_t status;
	};
	TempBuffer<TempPacket> mTempPacketBuffer;

	uint8_t getExtensionId(const std::shared_ptr<Track>& track);
};

} // namespace srtc

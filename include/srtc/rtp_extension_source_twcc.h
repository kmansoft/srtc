#pragma once

#include "srtc/rtp_extension_source.h"
#include "srtc/temp_buffer.h"
#include "srtc/util.h"

#include <cstdint>
#include <list>
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

	void onBeforeGeneratingRtpPacket(const std::shared_ptr<RtpPacket>& packet);
	void onBeforeSendingRtpPacket(const std::shared_ptr<RtpPacket>& packet, size_t generatedSize, size_t encryptedSize);
	void onPacketWasNacked(const std::shared_ptr<RtpPacket>& packet);

	void onReceivedRtcpPacket(uint32_t ssrc, ByteReader& reader);

	[[nodiscard]] bool getFeedbackSeq(const std::shared_ptr<RtpPacket>& packet, uint16_t& outSeq) const;

	[[nodiscard]] unsigned int getPacingSpreadMillis(const std::list<std::shared_ptr<RtpPacket>>& list,
													 float bandwidthScale,
													 unsigned int defaultValue) const;
	void updatePublishConnectionStats(PublishConnectionStats& stats) const;

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
	FixedTempBuffer<TempPacket> mTempPacketBuffer;

	[[nodiscard]] uint8_t getExtensionId(const std::shared_ptr<Track>& track) const;
};

} // namespace srtc

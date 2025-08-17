#include "srtc/packetizer_opus.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_extension_source.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/rtp_time_source.h"
#include "srtc/track.h"

namespace srtc
{

PacketizerOpus::PacketizerOpus(const std::shared_ptr<Track>& track)
	: Packetizer(track)
{
}

PacketizerOpus::~PacketizerOpus() = default;

std::list<std::shared_ptr<RtpPacket>> PacketizerOpus::generate(
	[[maybe_unused]] const std::shared_ptr<RtpExtensionSource>& simulcast,
	const std::shared_ptr<RtpExtensionSource>& twcc,
	[[maybe_unused]] size_t mediaProtectionOverhead,
    int64_t pts_usec,
	const srtc::ByteBuffer& frame)
{
	std::list<std::shared_ptr<RtpPacket>> result;

	// https://datatracker.ietf.org/doc/rfc7587

	const auto track = getTrack();

	const auto timeSource = track->getRtpTimeSource();
	const auto packetSource = track->getRtpPacketSource();

	const auto frameTimestamp = timeSource->getFrameTimestamp(pts_usec);

	auto payload = frame.copy();
	if (payload.size() > RtpPacket::kMaxPayloadSize) {
		payload.resize(RtpPacket::kMaxPayloadSize);
	}

	RtpExtension extension;
	if (twcc && twcc->wantsExtension(track, false, 0)) {
		RtpExtensionBuilder builder;
		twcc->addExtension(builder, track, false, 0);
		extension = builder.build();
	}

	const auto [rollover, sequence] = packetSource->getNextSequence();
	result.push_back(
		extension.empty()
			? std::make_shared<RtpPacket>(track, false, rollover, sequence, frameTimestamp, 0, std::move(payload))
			: std::make_shared<RtpPacket>(
				  track, false, rollover, sequence, frameTimestamp, 0, std::move(extension), std::move(payload)));

	return result;
}

} // namespace srtc

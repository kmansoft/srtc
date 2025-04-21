#include "srtc/packetizer_opus.h"
#include "srtc/track.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/rtp_time_source.h"

namespace srtc {

PacketizerOpus::PacketizerOpus(const std::shared_ptr<Track>& track)
    : Packetizer(track)
{
}

PacketizerOpus::~PacketizerOpus() = default;

std::list<std::shared_ptr<RtpPacket>> PacketizerOpus::generate([[maybe_unused]] const RtpExtension& extension,
                                                               [[maybe_unused]] bool addExtensionToAllPackets,
                                                               const srtc::ByteBuffer& frame)
{
    std::list<std::shared_ptr<RtpPacket>> result;

    // https://datatracker.ietf.org/doc/rfc7587

    const auto track = getTrack();

    const auto timeSource = track->getRtpTimeSource();
    const auto packetSource = track->getRtpPacketSource();

    const auto frameTimestamp = timeSource->getCurrTimestamp();

    auto payload = frame.copy();
    if (payload.size() > RtpPacket::kMaxPayloadSize) {
        payload.resize(RtpPacket::kMaxPayloadSize);
    }

    const auto [rollover, sequence] = packetSource->getNextSequence();
    result.push_back(
            std::make_shared<RtpPacket>(
                    track, false, rollover, sequence, frameTimestamp, std::move(payload)));

    return result;
}

}

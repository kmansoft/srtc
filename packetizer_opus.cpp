#include "srtc/packetizer_opus.h"
#include "srtc/track.h"
#include "srtc/rtp_packet_source.h"

namespace srtc {

PacketizerOpus::PacketizerOpus() = default;

PacketizerOpus::~PacketizerOpus() = default;

std::list<std::shared_ptr<RtpPacket>> PacketizerOpus::generate(const std::shared_ptr<Track>& track,
                                                               [[maybe_unused]] const RtpExtension& extension,
                                                               [[maybe_unused]] bool addExtensionToAllPackets,
                                                               const srtc::ByteBuffer& frame)
{
    std::list<std::shared_ptr<RtpPacket>> result;

    // https://datatracker.ietf.org/doc/rfc7587

    const auto packetSource = track->getPacketSource();
    const auto frameTimestamp = getNextTimestamp(48);

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

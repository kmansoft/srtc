#include "srtc/packetizer_opus.h"

namespace srtc {

PacketizerOpus::PacketizerOpus() = default;

PacketizerOpus::~PacketizerOpus() = default;

std::list<std::shared_ptr<RtpPacket>> PacketizerOpus::generate(const std::shared_ptr<Track>& track,
                                                               const srtc::ByteBuffer& frame)
{
    std::list<std::shared_ptr<RtpPacket>> result;

    // https://datatracker.ietf.org/doc/rfc7587

    const auto frameTimestamp = getNextTimestamp(48);

    auto payload = frame.copy();
    if (payload.size() > RtpPacket::kMaxPayloadSize) {
        payload.resize(RtpPacket::kMaxPayloadSize);
    }

    result.push_back(
            std::make_shared<RtpPacket>(
                    track, false, getNextSequence(), frameTimestamp, std::move(payload)));

    return result;
}

}

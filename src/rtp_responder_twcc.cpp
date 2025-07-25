#include "srtc/rtp_responder_twcc.h"
#include "srtc/logging.h"
#include "srtc/rtp_extension.h"
#include "srtc/rtp_packet.h"
#include "srtc/rtp_std_extensions.h"
#include "srtc/sdp_answer.h"
#include "srtc/sdp_offer.h"
#include "srtc/track.h"

#define LOG(level, ...) srtc::log(level, "TWCC", __VA_ARGS__)

namespace
{

uint8_t findTWCCExtension(const srtc::ExtensionMap& map)
{
    return map.findByName(srtc::RtpStandardExtensions::kExtGoogleTWCC);
}

} // namespace

namespace srtc
{

RtpResponderTWCC::RtpResponderTWCC(uint8_t nVideoExtTWCC, uint8_t nAudioExtTWCC)
    : mVideoExtTWCC(nVideoExtTWCC)
    , mAudioExtTWCC(nAudioExtTWCC)
{
}

RtpResponderTWCC::~RtpResponderTWCC() = default;

std::shared_ptr<RtpResponderTWCC> RtpResponderTWCC::factory(const std::shared_ptr<SdpOffer>& offer,
                                                            const std::shared_ptr<SdpAnswer>& answer)
{
    if (offer->getDirection() != Direction::Subscribe) {
        return {};
    }

    uint8_t nVideoExtTWCC = 0;
    if (answer->hasVideoMedia()) {
        nVideoExtTWCC = findTWCCExtension(answer->getVideoExtensionMap());
        if (nVideoExtTWCC == 0) {
            return {};
        }
    }

    uint8_t nAudioExtTWCC = 0;
    if (answer->hasAudioMedia()) {
        nAudioExtTWCC = findTWCCExtension(answer->getAudioExtensionMap());
        if (nAudioExtTWCC == 0) {
            return {};
        }
    }

    return std::make_shared<RtpResponderTWCC>(nVideoExtTWCC, nAudioExtTWCC);
}

void RtpResponderTWCC::onMediaPacket(const std::shared_ptr<RtpPacket>& packet)
{
    const auto track = packet->getTrack();
    const auto nExtId = getExtensionId(track);
    if (nExtId == 0) {
        return;
    }

    const auto& extension = packet->getExtension();
    const auto seq = extension.findU16(nExtId);
    if (!seq.has_value()) {
        return;
    }

    std::printf("Extracted TWCC seq = %u\n", seq.value());
}

uint8_t RtpResponderTWCC::getExtensionId(const std::shared_ptr<Track>& track) const
{
    const auto media = track->getMediaType();
    if (media == MediaType::Video) {
        return mVideoExtTWCC;
    } else if (media == MediaType::Audio) {
        return mAudioExtTWCC;
    }
    return 0;
}

} // namespace srtc
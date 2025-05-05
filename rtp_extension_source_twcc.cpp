#include "srtc/rtp_extension_source_twcc.h"
#include "srtc/extension_map.h"
#include "srtc/logging.h"
#include "srtc/rtp_extension_builder.h"
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

RtpExtensionSourceTWCC::RtpExtensionSourceTWCC(uint8_t nVideoExtTWCC, uint8_t nAudioExtTWCC)
    : mVideoExtTWCC(nVideoExtTWCC)
    , mAudioExtTWCC(nAudioExtTWCC)
    , mNextPacketSEQ(1)
{
}

RtpExtensionSourceTWCC::~RtpExtensionSourceTWCC() = default;

std::shared_ptr<RtpExtensionSourceTWCC> RtpExtensionSourceTWCC::factory(const std::shared_ptr<SdpOffer>& offer,
                                                                        const std::shared_ptr<SdpAnswer>& answer)
{
    const auto& config = offer->getConfig();
    if (!config.enableTWCC) {
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

    return std::make_shared<RtpExtensionSourceTWCC>(nVideoExtTWCC, nAudioExtTWCC);
}

bool RtpExtensionSourceTWCC::wants(const std::shared_ptr<Track>& track,
                                   [[maybe_unused]] bool isKeyFrame,
                                   [[maybe_unused]] int packetNumber)
{
    const auto media = track->getMediaType();
    return media == MediaType::Video && mVideoExtTWCC != 0 || media == MediaType::Audio && mAudioExtTWCC != 0;
}

void RtpExtensionSourceTWCC::add(RtpExtensionBuilder& builder,
                                 const std::shared_ptr<Track>& track,
                                 [[maybe_unused]] bool isKeyFrame,
                                 [[maybe_unused]] int packetNumber)
{
    const auto seq = mNextPacketSEQ;
    mNextPacketSEQ += 1;

    const auto media = track->getMediaType();
    if (const auto id = mVideoExtTWCC; media == MediaType::Video && id != 0) {
        // Video media
        LOG(SRTC_LOG_V, "Adding SEQ=%u to video", seq);
        builder.addU16Value(id, seq);
    } else if (const auto id = mAudioExtTWCC; media == MediaType::Audio && id != 0) {
        // Audio media
        LOG(SRTC_LOG_V, "Adding SEQ=%u to audio", seq);
        builder.addU16Value(id, seq);
    }
}

void RtpExtensionSourceTWCC::updateForRtx(RtpExtensionBuilder& builder, const std::shared_ptr<Track>& track)
{
    const auto seq = mNextPacketSEQ;
    mNextPacketSEQ += 1;

    const auto media = track->getMediaType();
    if (const auto id = mVideoExtTWCC; media == MediaType::Video && id != 0) {
        // Video media
        LOG(SRTC_LOG_V, "Adding SEQ=%u to resend video", seq);
        builder.addOrReplaceU16Value(id, seq);
    } else if (const auto id = mAudioExtTWCC; media == MediaType::Audio && id != 0) {
        // Audio media
        LOG(SRTC_LOG_V, "Adding SEQ=%u to resend audio", seq);
        builder.addOrReplaceU16Value(id, seq);
    }
}

} // namespace srtc
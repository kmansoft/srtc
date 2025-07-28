#include "srtc/rtp_responder_twcc.h"
#include "srtc/logging.h"
#include "srtc/rtcp_packet.h"
#include "srtc/rtp_extension.h"
#include "srtc/rtp_packet.h"
#include "srtc/rtp_std_extensions.h"
#include "srtc/sdp_answer.h"
#include "srtc/sdp_offer.h"
#include "srtc/track.h"
#include "srtc/twcc_subscribe.h"

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
    : mPacketHistory(std::make_unique<twcc::SubscribePacketHistory>(getStableTimeMicros()))
    , mVideoExtTWCC(nVideoExtTWCC)
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
    const auto ext_id = getExtensionId(track);
    if (ext_id == 0) {
        return;
    }

    const auto& extension = packet->getExtension();
    const auto seq = extension.findU16(ext_id);
    if (!seq.has_value()) {
        return;
    }

    const auto now = getStableTimeMicros();
    mPacketHistory->saveIncomingPacket(seq.value(), now);
}

std::vector<std::shared_ptr<RtcpPacket>> RtpResponderTWCC::run(const std::shared_ptr<Track>& track)
{
    std::vector<std::shared_ptr<RtcpPacket>> list;

    const auto now_micros = getStableTimeMicros();
    if (mPacketHistory->isTimeToGenerate(now_micros)) {
        const auto raw_list = mPacketHistory->generate(now_micros);

        for (const auto& raw : raw_list) {
            ByteBuffer payload;
            ByteWriter writer(payload);

            const auto ssrc = track->getSSRC();
            writer.writeU32(ssrc);
            writer.write(raw);

            list.push_back(
                std::make_shared<RtcpPacket>(ssrc, 15 /* TWCC */, RtcpPacket::kFeedback, std::move(payload)));
        }
    }

    return list;
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
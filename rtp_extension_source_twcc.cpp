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

// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.3
constexpr auto kTWCC_CHUNK_RUN_LENGTH = 0;
// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
constexpr auto kTWC_CHUNK_STATUS_VECTOR = 1;

// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.1
constexpr auto kTWCC_SYMBOL_NOT_RECEIVED = 0;
constexpr auto kTWCC_SYMBOL_RECEIVED_SMALL_DELTA = 1;
constexpr auto kTWCC_SYMBOL_RECEIVED_LARGE_DELTA = 2;
constexpr auto kTWCC_SYMBOL_RECEIVED_NO_TS = 3;

struct TWCCHeader {
    uint16_t base_seq_number;
    uint16_t packet_status_count;
    uint32_t reference_time;
    uint8_t fb_pkt_count;
};

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
    if (!config.enable_bwe) {
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

void RtpExtensionSourceTWCC::onReceivedRtcpPacket(uint32_t ssrc, ByteReader& reader)
{
    if (reader.remaining() < 8) {
        LOG(SRTC_LOG_E, "RTCP TWCC packet too small");
        return;
    }

    const uint16_t base_seq_number = reader.readU16();
    const uint16_t packet_status_count = reader.readU16();
    const uint32_t reference_time_and_fb_pkt_count = reader.readU32();

    const TWCCHeader header = { base_seq_number,
                                packet_status_count,
                                reference_time_and_fb_pkt_count >> 8,
                                static_cast<uint8_t>(reference_time_and_fb_pkt_count & 0xFF) };

    LOG(SRTC_LOG_V,
        "RTCP TWCC packet: base_seq_number=%u, packet_status_count=%u, reference_time=%u, fb_pkt_count=%u",
        header.base_seq_number,
        header.packet_status_count,
        header.reference_time,
        header.fb_pkt_count);

    const uint16_t past_end_seq_number = header.base_seq_number + header.packet_status_count;
    for (uint16_t seq_number = header.base_seq_number; seq_number != past_end_seq_number; /* do not increment here */) {
        if (reader.remaining() < 2) {
            LOG(SRTC_LOG_E, "RTCP TWCC packet too small");
            return;
        }

        const auto chunkHeader = reader.readU16();
        const auto chunkType = (chunkHeader >> 15) & 0x01;

        if (chunkType == kTWCC_CHUNK_RUN_LENGTH) {
            // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.3
            const auto symbol = (chunkHeader >> 13) & 0x03;
            const auto runLength = chunkHeader & 0x1FFF;
            LOG(SRTC_LOG_V, "RTCP TWCC packet: run_length=%u, status_symbol=%u", runLength, symbol);

            const uint16_t remaining = past_end_seq_number - seq_number;
            if (remaining < runLength || remaining > 0x1FFF) {
                LOG(SRTC_LOG_E, "RTCP TWCC packet: run_length %u is too large, remaining %u", runLength, remaining);
                break;
            }

            for (uint16_t j = 0; j < runLength; ++j) {
                LOG(SRTC_LOG_V, "RTCP TWCC packet: seq_number=%u, status_symbol=%u", seq_number, symbol);
                seq_number += 1;
                if (seq_number == past_end_seq_number) {
                    break;
                }
            }
        } else if (chunkType == kTWC_CHUNK_STATUS_VECTOR && ((chunkHeader >> 14) & 0x01) == 0) {
            // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
            for (uint16_t shift = 14; shift != 0; shift -= 1) {
                const auto symbol =
                    ((chunkHeader >> (shift - 1)) & 0x01) ? kTWCC_SYMBOL_RECEIVED_NO_TS : kTWCC_SYMBOL_NOT_RECEIVED;
                LOG(SRTC_LOG_V, "RTCP TWCC packet: seq_number=%u, status_symbol=%u", seq_number, symbol);
                seq_number += 1;
                if (seq_number == past_end_seq_number) {
                    break;
                }
            }
        } else if (chunkType == kTWC_CHUNK_STATUS_VECTOR && ((chunkHeader >> 14) & 0x01) == 1) {
            // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
            for (uint16_t shift = 14; shift != 0; shift -= 2) {
                const auto symbol = (chunkHeader >> (shift - 2)) & 0x03;
                LOG(SRTC_LOG_V, "RTCP TWCC packet: seq_number=%u, status_symbol=%u", seq_number, symbol);
                seq_number += 1;
                if (seq_number == past_end_seq_number) {
                    break;
                }
            }
        } else {
            LOG(SRTC_LOG_E, "RTCP TWCC packet: unknown chunk type %u", chunkType);
            break;
        }
    }
}

} // namespace srtc
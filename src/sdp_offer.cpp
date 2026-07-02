#include <cassert>
#include <cstring>
#include <sstream>
#include <string>

#include "srtc/rtp_std_extensions.h"
#include "srtc/sdp_offer.h"
#include "srtc/x509_certificate.h"

namespace
{

const char* codec_to_string(srtc::Codec codec)
{
    switch (codec) {
    case srtc::Codec::AV1:
        return "AV1/90000";
    case srtc::Codec::VP8:
        return "VP8/90000";
    case srtc::Codec::VP9:
        return "VP9/90000";
    case srtc::Codec::H264:
        return "H264/90000";
    case srtc::Codec::H265:
        return "H265/90000";
    case srtc::Codec::Opus:
        return "opus/48000/2";
    default:
        assert(false);
        return "-";
    }
}

std::string list_to_string(size_t start, size_t end)
{
    std::stringstream ss;
    for (auto i = start; i < end; i += 1) {
        if (i != start) {
            ss << " ";
        }
        ss << i;
    }
    ss << std::flush;

    return ss.str();
}

constexpr uint16_t kSctpPort = 5000;
constexpr uint32_t kSctpMaxMessageSize = 262144;

} // namespace

namespace srtc
{

SdpOffer::SdpOffer(Direction direction, const Config& config, const std::vector<MediaLine>& media)
    : mRandomGenerator(0, 0x7ffffffe)
    , mDirection(direction)
    , mConfig(config)
    , mMediaLineList(media)
    , mOriginId((static_cast<uint64_t>(mRandomGenerator.next()) << 32) | mRandomGenerator.next())
    , mIceUfrag(generateRandomString(8))
    , mIcePassword(generateRandomString(24))
    , mCert(std::make_shared<X509Certificate>())
{
}

Direction SdpOffer::getDirection() const
{
    return mDirection;
}

const SdpOffer::Config& SdpOffer::getConfig() const
{
    return mConfig;
}

std::pair<std::string, Error> SdpOffer::generate()
{
    const bool hasMedia = !mMediaLineList.empty();
    const bool hasData = !mConfig.data_channels.empty();

    if (!hasMedia && !hasData) {
        return { "", { Error::Code::InvalidData, "No video, audio, or data channels configured" } };
    }

    std::stringstream ss;

    ss << "v=0" << std::endl;
    ss << "o=- " << mOriginId << " 2 IN IP4 127.0.0.1" << std::endl;
    ss << "s=-" << std::endl;
    ss << "t=0 0" << std::endl;
    ss << "a=extmap-allow-mixed" << std::endl;
    ss << "a=msid-semantic: WMS" << std::endl;

    // Bundle
    {
        const auto sectionCount = mMediaLineList.size() + (hasData ? 1 : 0);
        if (sectionCount > 1) {
            ss << "a=group:BUNDLE";
            for (size_t i = 0; i < sectionCount; i += 1) {
                if (i < mMediaLineList.size()) {
                    ss << " " << mMediaLineList[i].id;
                } else {
                    ss << " datachannel";
                }
            }
            ss << std::endl;
        }
    }

    uint32_t payloadId = 96;

    // Media lines
    for (const auto& mediaLine : mMediaLineList) {
        const auto& codecList = mediaLine.codec_list;
        if (codecList.empty()) {
            return { "", { Error::Code::InvalidData, "A media line is present but has no codecs" } };
        }

        for (const auto& codec : codecList) {
            if (mediaLine.mediaType == MediaType::Audio) {
                if (!isAudioCodec(codec.codec)) {
                    return {
                        "", { Error::Code::InvalidData, "A media line with type audio has a codec that's not audio" }
                    };
                }
            } else if (mediaLine.mediaType == MediaType::Video) {
                if (!isVideoCodec(codec.codec)) {
                    return {
                        "", { Error::Code::InvalidData, "A media line with type video has a codec that's not video" }
                    };
                }
            }
        }

        mMediaLineGeneratedList.emplace_back(mediaLine.id);
        auto& mediaLineGenerated = mMediaLineGeneratedList.back();
        mediaLineGenerated.mediaType = mediaLine.mediaType;

        ss << "m=";
        if (mediaLine.mediaType == MediaType::Video) {
            ss << "video";
        } else {
            ss << "audio";
        }
        ss << " 9 UDP/TLS/RTP/SAVPF ";

        if (mConfig.enable_rtx) {
            ss << list_to_string(payloadId, payloadId + codecList.size() * 2) << std::endl;
        } else {
            ss << list_to_string(payloadId, payloadId + codecList.size()) << std::endl;
        }

        ss << "c=IN IP4 0.0.0.0" << std::endl;
        ss << "a=rtcp:9 IN IP4 0.0.0.0" << std::endl;
        ss << "a=fingerprint:sha-256 " << mCert->getSha256FingerprintHex() << std::endl;
        ss << "a=ice-ufrag:" << mIceUfrag << std::endl;
        ss << "a=ice-pwd:" << mIcePassword << std::endl;
        ss << "a=setup:actpass" << std::endl;
        ss << "a=mid:" << mediaLine.id << std::endl;

        if (mDirection == Direction::Publish) {
            ss << "a=sendonly" << std::endl;
        } else if (mDirection == Direction::Subscribe) {
            ss << "a=recvonly" << std::endl;
        } else {
            assert(false);
        }

        ss << "a=rtcp-mux" << std::endl;
        ss << "a=rtcp-rsize" << std::endl;

        for (const auto& item : codecList) {
            ss << "a=rtpmap:" << payloadId << " " << codec_to_string(item.codec) << std::endl;
            if (item.codec == Codec::H264) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%06x", item.profile_level_id);

                ss << "a=fmtp:" << payloadId
                   << " level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=" << buf << std::endl;
            } else if (item.codec == Codec::Opus) {
                ss << "a=fmtp:" << payloadId << " minptime=" << item.minptime << ";stereo=" << (item.stereo ? 1 : 0)
                   << ";useinbandfec=1" << std::endl;
            }

            ss << "a=rtcp-fb:" << payloadId << " nack" << std::endl;

            if (mediaLineGenerated.mediaType == MediaType::Video) {
                ss << "a=rtcp-fb:" << payloadId << " nack pli" << std::endl;
            }

            if (mConfig.enable_rtx) {
                const auto payloadIdRtx = payloadId + 1;
                ss << "a=rtpmap:" << payloadIdRtx << " rtx/90000" << std::endl;
                ss << "a=fmtp:" << payloadIdRtx << " apt=" << payloadId << std::endl;

                payloadId += 2;
            } else {
                payloadId += 1;
            }
        }

        const auto msid = generateRandomUUID();

        const auto& layerList = mediaLine.layer_list;
        if (layerList.empty() || mDirection == Direction::Subscribe) {
            // No simulcast or subscribe
            if (mConfig.enable_bwe || mDirection == Direction::Subscribe) {
                ss << "a=extmap:14 " << RtpStandardExtensions::kExtGoogleTWCC << std::endl;
            }

            mediaLineGenerated.ssrc = 1 + mRandomGenerator.next();

            ss << "a=ssrc:" << mediaLineGenerated.ssrc << " cname:" << mConfig.cname << std::endl;
            ss << "a=ssrc:" << mediaLineGenerated.ssrc << " msid:" << mConfig.cname << " " << msid << std::endl;

            if (mConfig.enable_rtx) {
                mediaLineGenerated.rtx = 1 + mRandomGenerator.next();

                ss << "a=ssrc:" << mediaLineGenerated.rtx << " cname:" << mConfig.cname << std::endl;
                ss << "a=ssrc:" << mediaLineGenerated.rtx << " msid:" << mConfig.cname << " " << msid << std::endl;

                // https://groups.google.com/g/discuss-webrtc/c/0OVDV6I3SRo
                ss << "a=ssrc-group:FID " << mediaLineGenerated.ssrc << " " << mediaLineGenerated.rtx << std::endl;
            }
        } else {
            // Simulcast
            ss << "a=extmap:1 " << RtpStandardExtensions::kExtSdesMid << std::endl;
            ss << "a=extmap:2 " << RtpStandardExtensions::kExtSdesRtpStreamId << std::endl;
            if (mConfig.enable_rtx) {
                ss << "a=extmap:3 " << RtpStandardExtensions::kExtSdesRtpRepairedStreamId << std::endl;
            }
            ss << "a=extmap:4 " << RtpStandardExtensions::kExtGoogleVLA << std::endl;

            if (mConfig.enable_bwe) {
                ss << "a=extmap:14 " << RtpStandardExtensions::kExtGoogleTWCC << std::endl;
            }

            for (const auto& layer : layerList) {
                ss << "a=rid:" << layer.name << " send";

                if (mConfig.enable_rfc8851) {
                    ss << " max-br=" << layer.kilobits_per_second * 1024 << ";max-width=" << layer.width
                       << ";max-height=" << layer.height << ";max-fps=" << layer.frames_per_second;
                }

                ss << std::endl;
            }

            ss << "a=simulcast:send";
            for (size_t i = 0; i < layerList.size(); i += 1) {
                if (i == 0) {
                    ss << " ";
                } else {
                    ss << ";";
                }
                ss << layerList[i].name;
            }
            ss << std::endl;

            for (const auto& layer : layerList) {
                mediaLineGenerated.layer.emplace_back(layer.name);
                auto& layerGenerated = mediaLineGenerated.layer.back();
                layerGenerated.ssrc = 1 + mRandomGenerator.next();

                ss << "a=ssrc:" << layerGenerated.ssrc << " cname:" << mConfig.cname << std::endl;
                ss << "a=ssrc:" << layerGenerated.ssrc << " msid:" << mConfig.cname << " " << msid << std::endl;

                if (mConfig.enable_rtx) {
                    layerGenerated.rtx = 1 + mRandomGenerator.next();

                    ss << "a=ssrc:" << layerGenerated.rtx << " cname:" << mConfig.cname << std::endl;
                    ss << "a=ssrc:" << layerGenerated.rtx << " msid:" << mConfig.cname << " " << msid << std::endl;

                    ss << "a=ssrc-group:FID " << layerGenerated.ssrc << " " << layerGenerated.rtx << std::endl;
                }
            }
        }
    }

    // Data channels
    if (hasData) {
        ss << "m=application 9 UDP/DTLS/SCTP webrtc-datachannel" << std::endl;
        ss << "c=IN IP4 0.0.0.0" << std::endl;
        ss << "a=fingerprint:sha-256 " << mCert->getSha256FingerprintHex() << std::endl;
        ss << "a=ice-ufrag:" << mIceUfrag << std::endl;
        ss << "a=ice-pwd:" << mIcePassword << std::endl;
        ss << "a=setup:actpass" << std::endl;
        ss << "a=mid:datachannel" << std::endl;
        ss << "a=sctp-port:" << kSctpPort << std::endl;
        ss << "a=max-message-size:" << kSctpMaxMessageSize << std::endl;
    }

    return { ss.str(), Error::OK };
}

std::string SdpOffer::getIceUFrag() const
{
    return mIceUfrag;
}

std::string SdpOffer::getIcePassword() const
{
    return mIcePassword;
}

std::shared_ptr<X509Certificate> SdpOffer::getCertificate() const
{
    return mCert;
}

std::optional<std::vector<SimulcastLayer>> SdpOffer::getVideoSimulcastLayerList(const std::string& mediaId) const
{
    for (const auto& mediaLine : mMediaLineList) {
        if (mediaLine.id == mediaId) {
            if (mediaLine.layer_list.empty()) {
                return std::nullopt;
            }
            return mediaLine.layer_list;
        }
    }

    return std::nullopt;
}

std::pair<uint32_t, uint32_t> SdpOffer::getMediaSSRC(const std::string& mediaId) const
{
    for (const auto& mediaItem : mMediaLineGeneratedList) {
        if (mediaItem.mediaId == mediaId) {
            return { mediaItem.ssrc, mediaItem.rtx };
        }
    }

    return {};
}

std::pair<uint32_t, uint32_t> SdpOffer::getVideoSimulastSSRC(const std::string& mediaId, const std::string& name) const
{
    for (const auto& mediaItem : mMediaLineGeneratedList) {
        if (mediaItem.mediaId == mediaId) {
            for (const auto& layerItem : mediaItem.layer) {
                if (layerItem.name == name) {
                    return std::make_pair(layerItem.ssrc, layerItem.rtx);
                }
            }
        }
    }

    return {};
}

bool SdpOffer::hasDataChannel() const
{
    return !mConfig.data_channels.empty();
}

uint16_t SdpOffer::getSctpPort() const
{
    return kSctpPort;
}

uint32_t SdpOffer::getSctpMaxMessageSize() const
{
    return kSctpMaxMessageSize;
}

std::string SdpOffer::generateRandomUUID()
{
    static const char* const ALPHABET = "0123456789abcdef";

    std::string res;
    for (size_t i = 0; i < 16; i += 1) {
        switch (i) {
        case 4:
        case 6:
        case 8:
        case 10:
            res += '-';
            break;
        default:
            break;
        }
        res += ALPHABET[mRandomGenerator.next() & 0x0F];
        res += ALPHABET[mRandomGenerator.next() & 0x0F];
    }

    return res;
}

std::string SdpOffer::generateRandomString(size_t len)
{
    static const char* const ALPHABET = "abcdefghijklmnopqrstuvwxyz0123456789";

    const auto alphabetLen = std::strlen(ALPHABET);

    std::string res;
    res.reserve(len);

    for (auto i = 0u; i < len; i += 1) {
        res += ALPHABET[mRandomGenerator.next() % alphabetLen];
    }

    return res;
}

} // namespace srtc

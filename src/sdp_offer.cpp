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
    case srtc::Codec::VP8:
        return "VP8/90000";
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

} // namespace

namespace srtc
{

SdpOffer::SdpOffer(Direction direction,
                   const Config& config,
                   const std::optional<VideoConfig>& videoConfig,
                   const std::optional<AudioConfig>& audioConfig)
    : mRandomGenerator(0, 0x7ffffffe)
    , mDirection(direction)
    , mConfig(config)
    , mVideoConfig(videoConfig)
    , mAudioConfig(audioConfig)
    , mOriginId((static_cast<uint64_t>(mRandomGenerator.next()) << 32) | mRandomGenerator.next())
    , mVideoSSRC(1 + mRandomGenerator.next())
    , mRtxVideoSSRC(1 + mRandomGenerator.next())
    , mAudioSSRC(1 + mRandomGenerator.next())
    , mRtxAudioSSRC(1 + mRandomGenerator.next())
    , mVideoMSID(generateRandomUUID())
    , mAudioMSID(generateRandomUUID())
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
    if (!mVideoConfig.has_value() && !mAudioConfig.has_value()) {
        return { "", { Error::Code::InvalidData, "No video and no audio configured" } };
    }

    std::stringstream ss;

    ss << "v=0" << std::endl;
    ss << "o=- " << mOriginId << " 2 IN IP4 127.0.0.1" << std::endl;
    ss << "s=-" << std::endl;
    ss << "t=0 0" << std::endl;
    ss << "a=extmap-allow-mixed" << std::endl;
    ss << "a=msid-semantic: WMS" << std::endl;

    if (mVideoConfig.has_value() && mAudioConfig.has_value()) {
        ss << "a=group:BUNDLE 0 1" << std::endl;
    }

    uint32_t mid = 0;
    uint32_t payloadId = 96;

    // Video
    if (mVideoConfig.has_value()) {
        const auto& list = mVideoConfig->codec_list;
        if (list.empty()) {
            return { "", { Error::Code::InvalidData, "The video config list is present but empty" } };
        }

        if (mConfig.enable_rtx) {
            ss << "m=video 9 UDP/TLS/RTP/SAVPF " << list_to_string(payloadId, payloadId + list.size() * 2) << std::endl;
        } else {
            ss << "m=video 9 UDP/TLS/RTP/SAVPF " << list_to_string(payloadId, payloadId + list.size()) << std::endl;
        }

        ss << "c=IN IP4 0.0.0.0" << std::endl;
        ss << "a=rtcp:9 IN IP4 0.0.0.0" << std::endl;
        ss << "a=fingerprint:sha-256 " << mCert->getSha256FingerprintHex() << std::endl;
        ss << "a=ice-ufrag:" << mIceUfrag << std::endl;
        ss << "a=ice-pwd:" << mIcePassword << std::endl;
        ss << "a=setup:actpass" << std::endl;
        ss << "a=mid:" << mid << std::endl;
        mid += 1;

        if (mDirection == Direction::Publish) {
            ss << "a=sendonly" << std::endl;
        } else if (mDirection == Direction::Subscribe) {
            ss << "a=recvonly" << std::endl;
        } else {
            assert(false);
        }

        ss << "a=rtcp-mux" << std::endl;
        ss << "a=rtcp-rsize" << std::endl;

        for (const auto& item : list) {
            ss << "a=rtpmap:" << payloadId << " " << codec_to_string(item.codec) << std::endl;
            if (item.codec == Codec::H264) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%06x", item.profile_level_id);

                ss << "a=fmtp:" << payloadId
                   << " level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=" << buf << std::endl;
            }
            ss << "a=rtcp-fb:" << payloadId << " nack" << std::endl;
            ss << "a=rtcp-fb:" << payloadId << " nack pli" << std::endl;

            if (mConfig.enable_rtx) {
                const auto payloadIdRtx = payloadId + 1;
                ss << "a=rtpmap:" << payloadIdRtx << " rtx/90000" << std::endl;
                ss << "a=fmtp:" << payloadIdRtx << " apt=" << payloadId << std::endl;

                payloadId += 2;
            } else {
                payloadId += 1;
            }
        }

        const auto& layerList = mVideoConfig->simulcast_layer_list;
        if (layerList.empty() || mDirection == Direction::Subscribe) {
            // No simulcast or subscribe
            if (mConfig.enable_bwe || mDirection == Direction::Subscribe) {
                ss << "a=extmap:14 " << RtpStandardExtensions::kExtGoogleTWCC << std::endl;
            }

            ss << "a=ssrc:" << mVideoSSRC << " cname:" << mConfig.cname << std::endl;
            ss << "a=ssrc:" << mVideoSSRC << " msid:" << mConfig.cname << " " << mVideoMSID << std::endl;

            if (mConfig.enable_rtx) {
                ss << "a=ssrc:" << mRtxVideoSSRC << " cname:" << mConfig.cname << std::endl;
                ss << "a=ssrc:" << mRtxVideoSSRC << " msid:" << mConfig.cname << " " << mVideoMSID << std::endl;

                // https://groups.google.com/g/discuss-webrtc/c/0OVDV6I3SRo
                ss << "a=ssrc-group:FID " << mVideoSSRC << " " << mRtxVideoSSRC << std::endl;
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
                ss << "a=rid:" << layer.name << " send" << std::endl;
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
                const auto videoSSRC = 1 + mRandomGenerator.next();
                const auto videoRtxSSRC = 1 + mRandomGenerator.next();

                mLayerSSRC.push_back({ layer.name, videoSSRC, videoRtxSSRC });

                ss << "a=ssrc:" << videoSSRC << " cname:" << mConfig.cname << std::endl;
                ss << "a=ssrc:" << videoSSRC << " msid:" << mConfig.cname << " " << mVideoMSID << std::endl;

                if (mConfig.enable_rtx) {
                    ss << "a=ssrc:" << videoRtxSSRC << " cname:" << mConfig.cname << std::endl;
                    ss << "a=ssrc:" << videoRtxSSRC << " msid:" << mConfig.cname << " " << mVideoMSID << std::endl;

                    ss << "a=ssrc-group:FID " << videoSSRC << " " << videoRtxSSRC << std::endl;
                }
            }
        }
    }

    // Audio
    if (mAudioConfig.has_value()) {
        const auto& list = mAudioConfig->codec_list;
        if (list.empty()) {
            return { "", { Error::Code::InvalidData, "The audio config list is present but empty" } };
        }

        if (mConfig.enable_rtx) {
            ss << "m=audio 9 UDP/TLS/RTP/SAVPF " << list_to_string(payloadId, payloadId + list.size() * 2) << std::endl;
        } else {
            ss << "m=audio 9 UDP/TLS/RTP/SAVPF " << list_to_string(payloadId, payloadId + list.size()) << std::endl;
        }

        ss << "c=IN IP4 0.0.0.0" << std::endl;
        ss << "a=rtcp:9 IN IP4 0.0.0.0" << std::endl;
        ss << "a=fingerprint:sha-256 " << mCert->getSha256FingerprintHex() << std::endl;
        ss << "a=ice-ufrag:" << mIceUfrag << std::endl;
        ss << "a=ice-pwd:" << mIcePassword << std::endl;
        ss << "a=setup:actpass" << std::endl;
        ss << "a=mid:" << mid << std::endl;
        mid += 1;

        if (mDirection == Direction::Publish) {
            ss << "a=sendonly" << std::endl;
        } else if (mDirection == Direction::Subscribe) {
            ss << "a=recvonly" << std::endl;
        } else {
            assert(false);
        }

        ss << "a=rtcp-mux" << std::endl;
        ss << "a=rtcp-rsize" << std::endl;

        for (const auto& item : list) {
            ss << "a=rtpmap:" << payloadId << " " << codec_to_string(item.codec) << std::endl;
            if (item.codec == Codec::Opus) {
                ss << "a=fmtp:" << payloadId << " minptime=" << item.minptime << ";stereo=" << (item.stereo ? 1 : 0)
                   << ";useinbandfec=1" << std::endl;
            }

            if (mConfig.enable_rtx) {
                const auto payloadIdRtx = payloadId + 1;
                ss << "a=rtpmap:" << payloadIdRtx << " rtx/48000" << std::endl;
                ss << "a=fmtp:" << payloadIdRtx << " apt=" << payloadId << std::endl;

                payloadId += 2;
            } else {
                payloadId += 1;
            }
        }

        if (mConfig.enable_bwe || mDirection == Direction::Subscribe) {
            ss << "a=extmap:14 " << RtpStandardExtensions::kExtGoogleTWCC << std::endl;
        }

        ss << "a=ssrc:" << mAudioSSRC << " cname:" << mConfig.cname << std::endl;
        ss << "a=ssrc:" << mAudioSSRC << " msid:" << mConfig.cname << " " << mAudioMSID << std::endl;

        if (mConfig.enable_rtx) {
            ss << "a=ssrc:" << mRtxAudioSSRC << " cname:" << mConfig.cname << std::endl;
            ss << "a=ssrc:" << mRtxAudioSSRC << " msid:" << mConfig.cname << " " << mAudioMSID << std::endl;

            // https://groups.google.com/g/discuss-webrtc/c/0OVDV6I3SRo
            ss << "a=ssrc-group:FID " << mAudioSSRC << " " << mRtxAudioSSRC << std::endl;
        }
    }

    return { ss.str(), Error::OK };
}

std::optional<std::vector<SimulcastLayer>> SdpOffer::getVideoSimulcastLayerList() const
{
    if (mVideoConfig.has_value()) {
        return mVideoConfig->simulcast_layer_list;
    }

    return std::nullopt;
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

uint32_t SdpOffer::getVideoSSRC() const
{
    return mVideoSSRC;
}

uint32_t SdpOffer::getRtxVideoSSRC() const
{
    return mRtxVideoSSRC;
}

uint32_t SdpOffer::getAudioSSRC() const
{
    return mAudioSSRC;
}

uint32_t SdpOffer::getRtxAudioSSRC() const
{
    return mRtxAudioSSRC;
}

std::pair<uint32_t, uint32_t> SdpOffer::getVideoSimulastSSRC(const std::string& name) const
{
    for (const auto& item : mLayerSSRC) {
        if (item.name == name) {
            return std::make_pair(item.ssrc, item.rtx);
        }
    }

    return {};
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

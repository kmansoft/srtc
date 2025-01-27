#include <sstream>
#include <cassert>
#include <string>

#include "srtc/sdp_offer.h"
#include "srtc/x509_certificate.h"

namespace {

const char* codec_to_string(srtc::Codec codec) {
    switch (codec) {
        case srtc::Codec::H264:
            return "H264/90000";
        case srtc::Codec::Opus:
            return "opus/48000/2";
        default:
            assert(false);
            return "-";
    }
}

std::string list_to_string(uint32_t start, uint32_t end)
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

}

namespace srtc {

SdpOffer::SdpOffer(const OfferConfig& config,
                   const std::optional<PubVideoConfig>& videoConfig,
                   const std::optional<PubAudioConfig>& audioConfig)
   : mRandomGenerator(0, 0x7ffffffe)
   , mConfig(config)
   , mVideoConfig(videoConfig)
   , mAudioConfig(audioConfig)
   , mOriginId((static_cast<uint64_t>(mRandomGenerator.next()) << 32) | mRandomGenerator.next())
   , mVideoSSRC(1 + mRandomGenerator.next())
   , mAudioSSRC(1 + mRandomGenerator.next())
   , mVideoMSID(generateRandomUUID())
   , mAudioMSID(generateRandomUUID())
   , mIceUfrag(generateRandomString(8))
   , mIcePassword(generateRandomString(24))
   , mCert(std::make_shared<X509Certificate>())
{
}

std::pair<std::string, Error> SdpOffer::generate()
{
    if (!mVideoConfig.has_value() && !mAudioConfig.has_value()) {
        return { "", { Error::Code::InvalidData, "No video and no audio configured"} };
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
        const auto& list = mVideoConfig->list;
        if (list.empty()) {
            return { "", { Error::Code::InvalidData, "The video config list is present but empty"} };
        }

// #define ENABLE_RTX

#ifdef ENABLE_RTX
        ss << "m=video 9 UDP/TLS/RTP/SAVPF " << list_to_string(payloadId, payloadId + list.size() * 2) << std::endl;
#else
        ss << "m=video 9 UDP/TLS/RTP/SAVPF " << list_to_string(payloadId, payloadId + list.size()) << std::endl;
#endif
        ss << "c=IN IP4 0.0.0.0" << std::endl;
        ss << "a=rtcp:9 IN IP4 0.0.0.0" << std::endl;
        ss << "a=fingerprint:sha-256 " << mCert->getSha256FingerprintHex() << std::endl;
        ss << "a=ice-ufrag:" << mIceUfrag << std::endl;
        ss << "a=ice-pwd:" << mIcePassword << std::endl;
        ss << "a=setup:actpass" << std::endl;
        ss << "a=mid:" << mid << std::endl;
        mid += 1;
        ss
           << "a=extmap:4 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"
           << std::endl;
        ss << "a=sendonly" << std::endl;
        ss << "a=rtcp-mux" << std::endl;
        ss << "a=rtcp-rsize" << std::endl;

        for (const auto& item: list) {
            ss << "a=rtpmap:" << payloadId << " " << codec_to_string(item.codec) << std::endl;
            if (item.codec == Codec::H264) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%06x", item.profileLevelId);

                ss << "a=fmtp:" << payloadId
                   << " level-asymmetry-allowed=1;packetization-mode=1;profile-level-id="
                   << buf << std::endl;
            }
            ss << "a=rtcp-fb:" << payloadId << " nack" << std::endl;
            ss << "a=rtcp-fb:" << payloadId << " nack pli" << std::endl;

#ifdef ENABLE_RTX
            const auto payloadIdRtx = payloadId + 1;
            ss << "a=rtpmap:" << payloadIdRtx << " rtx/90000" << std::endl;
            ss << "a=fmtp:" << payloadIdRtx << " apt=" << payloadId << std::endl;
#endif
#ifdef ENABLE_RTX
            payloadId += 2;
#else
            payloadId += 1;
#endif
        }

        ss << "a=ssrc:" << mVideoSSRC << " cname:" << mConfig.cname << std::endl;
        ss << "a=ssrc:" << mVideoSSRC << " msid:" << mConfig.cname << " " << mVideoMSID << std::endl;
    }

    // Audio
    if (mAudioConfig.has_value()) {
        const auto &list = mAudioConfig->list;
        if (list.empty()) {
            return {"", {Error::Code::InvalidData, "The audio config list is present but empty"}};
        }

        ss << "m=audio 9 UDP/TLS/RTP/SAVPF " << list_to_string(payloadId, payloadId + list.size()) << std::endl;
        ss << "c=IN IP4 0.0.0.0" << std::endl;
        ss << "a=rtcp:9 IN IP4 0.0.0.0" << std::endl;
        ss << "a=fingerprint:sha-256 " << mCert->getSha256FingerprintHex() << std::endl;
        ss << "a=ice-ufrag:" << mIceUfrag << std::endl;
        ss << "a=ice-pwd:" << mIcePassword << std::endl;
        ss << "a=setup:actpass" << std::endl;
        ss << "a=mid:" << mid << std::endl;
        mid += 1;
        ss
           << "a=extmap:4 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"
           << std::endl;
        ss << "a=sendonly" << std::endl;
        ss << "a=rtcp-mux" << std::endl;
        ss << "a=rtcp-rsize" << std::endl;

        for (const auto& item: list) {
            if (item.codec == Codec::Opus) {
                ss << "a=rtpmap:" << payloadId << " " << codec_to_string(item.codec) << std::endl;
                ss << "a=fmtp:" << payloadId
                   << " minptime=" << item.minPacketTimeMs << ";useinbandfec=1" << std::endl;
            }

            payloadId += 1;
        }

        ss << "a=ssrc:" << mAudioSSRC << " cname:" << mConfig.cname << std::endl;
        ss << "a=ssrc:" << mAudioSSRC << " msid:" << mConfig.cname << " " << mAudioMSID << std::endl;
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

uint32_t SdpOffer::getVideoSSRC() const
{
    return mVideoSSRC;
}

uint32_t SdpOffer::getAudioSSRC() const
{
    return mAudioSSRC;
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

    for (auto i = 0; i < len; i += 1) {
        res += ALPHABET[mRandomGenerator.next() % alphabetLen];
    }

    return res;
}

}

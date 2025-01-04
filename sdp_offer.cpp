#include <sstream>
#include <cassert>

#include "srtc/sdp_offer.h"
#include "srtc/x509_certificate.h"

namespace {

const char* to_string(srtc::Codec codec) {
    switch (codec) {
        case srtc::Codec::H264:
            return "H264";
        case srtc::Codec::Opus:
            return "opus";
        default:
            assert(false);
            return "-";
    }
}

}

namespace srtc {

SdpOffer::SdpOffer(const OfferConfig& config,
                   const srtc::VideoConfig& videoConfig,
                   const std::optional<AudioConfig>& audioConfig)
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
    std::stringstream ss;

    ss << "v=0" << std::endl;
    ss << "o=- " << mOriginId << " 2 IN IP4 127.0.0.1" << std::endl;
    ss << "s=-" << std::endl;
    ss << "t=0 0" << std::endl;
    ss << "a=extmap-allow-mixed" << std::endl;
    ss << "a=msid-semantic: WMS" << std::endl;
    ss << "a=fingerprint:sha-256 " << mCert->getSha256FingerprintHex() << std::endl;
    ss << "a=ice-ufrag:" << mIceUfrag << std::endl;
    ss << "a=ice-pwd:" << mIcePassword << std::endl;
    ss << "a=setup:actpass" << std::endl;

    // Video
    const auto layer = mVideoConfig.layerList[0];
    const int payloadId = 96;

    ss << "m=video 9 UDP/TLS/RTP/SAVPF " << payloadId << std::endl;
    ss << "c=IN IP4 0.0.0.0" << std::endl;
    ss << "a=rtcp:9 IN IP4 0.0.0.0" << std::endl;
    ss << "a=mid:0" << std::endl;
    ss << "a=extmap:4 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01" << std::endl;
    ss << "a=sendonly" << std::endl;
    ss << "a=rtcp-mux" << std::endl;
    ss << "a=rtcp-rsize" << std::endl;
    ss << "a=rtpmap:" << payloadId << " " << to_string(layer.codec) << "/90000" << std::endl;
    if (layer.codec == Codec::H264) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%02x%04x", layer.profileId, layer.level);

        ss << "a=fmtp:" << payloadId
           << " level-asymmetry-allowed=1;packetization-mode=1;profile-level-id="
           << buf << std::endl;
    }
    ss << "a=ssrc:" << mVideoSSRC << " cname:" << mConfig.cname << std::endl;
    ss << "a=ssrc:" << mVideoSSRC << " msid:- " << mVideoMSID << std::endl;

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

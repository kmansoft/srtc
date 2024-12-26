#include <sstream>
#include <cassert>

#include "srtc/sdp_offer.h"

namespace {

const char* to_string(srtc::VideoCodec codec) {
    switch (codec) {
        case srtc::VideoCodec::H264:
            return "H264";
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
   : mConfig(config)
   , mVideoConfig(videoConfig)
   , mAudioConfig(audioConfig)
   , mRandomGenerator(0, 0x7fffffff)
   , mOriginId((static_cast<int64_t>(mRandomGenerator.next()) << 32) | mRandomGenerator.next())
   , mVideoSSRC(mRandomGenerator.next())
   , mAudioSSRC(mRandomGenerator.next())
   , mVideoMSID(generateRandomUUID())
   , mAudioMSID(generateRandomUUID())
   , mIceUfrag(generateRandomString(8))
   , mIcePassword(generateRandomString(24))
{
}

Error SdpOffer::generate(std::string& outSdpOffer)
{
    std::stringstream ss;

    ss << "v=0" << std::endl;
    ss << "o=- " << mOriginId << " 2 IN IP4 127.0.0.1" << std::endl;
    ss << "s=-" << std::endl;
    ss << "t=0 0" << std::endl;
    ss << "a=extmap-allow-mixed" << std::endl;
    ss << "a=msid-semantic: WMS" << std::endl;
    // TODO generate for real
    ss << "a=fingerprint:sha-256 3D:CA:CB:5C:57:0C:2E:B3:E1:ED:E1:31:45:15:DF:67:EF:26:E7:4A:3D:25:7C:C7:2F:0C:27:ED:06:EB:98:30" << std::endl;
    ss << "a=ice-ufrag:" << mIceUfrag << std::endl;
    ss << "a=ice-pwd:" << mIcePassword << std::endl;

    // Video
    const auto layer = mVideoConfig.layerList[0];
    const int layerId = 100;

    ss << "m=video 9 UDP/TLS/RTP/SAVPF " << layerId << std::endl;
    ss << "c=IN IP4 0.0.0.0" << std::endl;
    ss << "a=rtcp:9 IN IP4 0.0.0.0" << std::endl;
    ss << "a=mid:0" << std::endl;
    ss << "a=extmap:4 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01" << std::endl;
    ss << "a=sendonly" << std::endl;
    ss << "a=rtcp-mux" << std::endl;
    ss << "a=rtcp-rsize" << std::endl;
    if (layer.codec == VideoCodec::H264) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%02x%04x", layer.profileId, layer.level);

        ss << "a=rtpmap:" << layerId << " " << to_string(layer.codec) << "/90000" << std::endl;
        ss << "a=fmtp:" << layerId
           << " level-asymmetry-allowed=1;packetization-mode=1;profile-level-id="
           << buf << std::endl;
    } else {
        ss << "a=rtpmap:" << layerId << " " << to_string(layer.codec) << "/90000" << std::endl;
    }
    ss << "a=ssrc:" << mVideoSSRC << " cname:" << mConfig.cname << std::endl;
    ss << "a=ssrc:" << mVideoSSRC << " msid:- " << mVideoMSID << std::endl;

    outSdpOffer = ss.str();

    return Error::OK;
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

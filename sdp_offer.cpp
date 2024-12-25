#include "srtc/sdp_offer.h"

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

Error SdpOffer::generate(std::string &outSdpOffer)
{
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

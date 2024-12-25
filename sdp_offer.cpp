#include "srtc/sdp_offer.h"

namespace srtc {

SdpOffer::SdpOffer(const OfferConfig& config,
                   const srtc::VideoConfig& videoConfig,
                   const std::optional<AudioConfig>& audioConfig)
   : mConfig(config)
   , mVideoConfig(videoConfig)
   , mAudioConfig(audioConfig)
   , mRandomTwister(mRandomDevice())
   , mRandomDist(0, 0x7fffffff)
   , mOriginId((static_cast<int64_t>(mRandomDist(mRandomTwister)) << 32) | mRandomDist(mRandomTwister))
   , mVideoSSRC(mRandomDist(mRandomTwister))
   , mAudioSSRC(mRandomDist(mRandomTwister))
   , mIceUfrag(generateRandomString(16))
   , mIcePassword(generateRandomString(32))
{

}

std::string SdpOffer::generateRandomString(size_t len)
{
    static const char* const ALPHABET = "abcdefghijklmnopqrstuvwxyz0123456789";

    const auto alphabetLen = std::strlen(ALPHABET);

    std::string res;
    res.reserve(len);

    for (auto i = 0; i < len; i += 1) {
        res += ALPHABET[mRandomDist(mRandomTwister) % alphabetLen];
    }

    return res;
}

}

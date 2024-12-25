#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <random>

#include "srtc/srtc.h"
#include "srtc/error.h"
#include "srtc/random_generator.h"

namespace srtc {

struct OfferConfig {
};

struct VideoLayer {
    VideoCodec codec;
    uint32_t profileId;
    uint32_t level;
    // Below are for simulcast only
    std::string name;
    uint32_t width;
    uint32_t height;
    uint32_t bitsPerSecond;
};

struct VideoConfig {
    std::string cname;
    std::vector<VideoLayer> layerList;
};

struct AudioConfig {
    AudioCodec codec;
};

class SdpOffer {
public:
    SdpOffer(const OfferConfig& config,
             const VideoConfig& videoConfig,
             const std::optional<AudioConfig>& audioConfig);
    ~SdpOffer() = default;

    Error generate(std::string& outSdpOffer);

private:
    std::string generateRandomUUID();
    std::string generateRandomString(size_t len);

    const OfferConfig mConfig;
    const VideoConfig mVideoConfig;
    const std::optional<AudioConfig> mAudioConfig;

    RandomGenerator<int32_t> mRandomGenerator;

    const int64_t mOriginId;

    const int32_t mVideoSSRC;
    const int32_t mAudioSSRC;

    const std::string mVideoMSID;
    const std::string mAudioMSID;

    const std::string mIceUfrag;
    const std::string mIcePassword;
};

}

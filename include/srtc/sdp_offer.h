#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <random>

#include "srtc/srtc.h"

namespace srtc {

struct OfferConfig {

};

struct VideoLayer {
    VideoCodec codec;
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

    std::string generate();

private:
    std::string generateRandomString(size_t len);

    const OfferConfig mConfig;
    const VideoConfig mVideoConfig;
    const std::optional<AudioConfig> mAudioConfig;

    std::random_device mRandomDevice;
    std::mt19937 mRandomTwister;
    std::uniform_int_distribution<int32_t> mRandomDist;

    const int64_t mOriginId;

    const int32_t mVideoSSRC;
    const int32_t mAudioSSRC;

    const std::string mIceUfrag;
    const std::string mIcePassword;
};

}

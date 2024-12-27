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

class X509Certificate;

struct OfferConfig {
    std::string cname;
};

struct VideoLayer {
    Codec codec;
    uint32_t profileId;
    uint32_t level;
    // Below are for simulcast only
    std::string name;
    uint32_t width;
    uint32_t height;
    uint32_t bitsPerSecond;
};

struct VideoConfig {
    std::vector<VideoLayer> layerList;
};

struct AudioConfig {
    Codec codec;
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

    RandomGenerator<int32_t> mRandomGenerator;

    const OfferConfig mConfig;
    const VideoConfig mVideoConfig;
    const std::optional<AudioConfig> mAudioConfig;

    const int64_t mOriginId;

    const int32_t mVideoSSRC;
    const int32_t mAudioSSRC;

    const std::string mVideoMSID;
    const std::string mAudioMSID;

    const std::string mIceUfrag;
    const std::string mIcePassword;

    const std::shared_ptr<X509Certificate> mCert;
};

}

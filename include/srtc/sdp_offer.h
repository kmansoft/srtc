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

    [[nodiscard]] std::pair<std::string, Error> generate();

    [[nodiscard]] std::string getIceUFrag() const;
    [[nodiscard]] std::string getIcePassword() const;
    [[nodiscard]] std::shared_ptr<X509Certificate> getCertificate() const;

    [[nodiscard]] uint32_t getVideoSSRC() const;
    [[nodiscard]] uint32_t getAudioSSRC() const;

private:
    std::string generateRandomUUID();
    std::string generateRandomString(size_t len);

    RandomGenerator<uint32_t> mRandomGenerator;

    const OfferConfig mConfig;
    const VideoConfig mVideoConfig;
    const std::optional<AudioConfig> mAudioConfig;

    const uint64_t mOriginId;

    const uint32_t mVideoSSRC;
    const uint32_t mAudioSSRC;

    const std::string mVideoMSID;
    const std::string mAudioMSID;

    const std::string mIceUfrag;
    const std::string mIcePassword;

    const std::shared_ptr<X509Certificate> mCert;
};

}

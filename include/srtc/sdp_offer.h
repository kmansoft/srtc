#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <random>

#include "srtc/srtc.h"
#include "srtc/error.h"
#include "srtc/random_generator.h"
#include "srtc/optional.h"

namespace srtc {

class X509Certificate;

struct OfferConfig {
    std::string cname;
};

struct PubVideoCodecConfig {
    Codec codec;
    uint32_t profileLevelId;    // for h264
};

struct PubVideoConfig {
    std::vector<PubVideoCodecConfig> list;
};

struct PubAudioCodecConfig {
    Codec codec;
    uint32_t minPacketTimeMs;     // for Opus
};

struct PubAudioConfig {
    std::vector<PubAudioCodecConfig> list;
};

class SdpOffer {
public:
    SdpOffer(const OfferConfig& config,
             const srtc::optional<PubVideoConfig>& videoConfig,
             const srtc::optional<PubAudioConfig>& audioConfig);
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
    const srtc::optional<PubVideoConfig> mVideoConfig;
    const srtc::optional<PubAudioConfig> mAudioConfig;

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

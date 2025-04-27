#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <random>
#include <memory>

#include "srtc/srtc.h"
#include "srtc/error.h"
#include "srtc/random_generator.h"
#include "srtc/optional.h"
#include "srtc/simulcast_layer.h"

namespace srtc {

class X509Certificate;

struct OfferConfig {
    std::string cname;
};

struct PubVideoCodec {
    Codec codec;
    uint32_t profileLevelId;    // for h264
};

struct PubVideoConfig {
    std::vector<PubVideoCodec> codecList;
    std::vector<SimulcastLayer> simulcastLayerList;
};

struct PubAudioCodec {
    Codec codec;
    uint32_t minptime;
    bool stereo;
};

struct PubAudioConfig {
    std::vector<PubAudioCodec> codecList;
};

class SdpOffer {
public:
    SdpOffer(const OfferConfig& config,
             const srtc::optional<PubVideoConfig>& videoConfig,
             const srtc::optional<PubAudioConfig>& audioConfig);
    ~SdpOffer() = default;

    [[nodiscard]] std::pair<std::string, Error> generate();

    [[nodiscard]] srtc::optional<std::vector<SimulcastLayer>> getVideoSimulcastLayerList() const;
    [[nodiscard]] std::string getIceUFrag() const;
    [[nodiscard]] std::string getIcePassword() const;
    [[nodiscard]] std::shared_ptr<X509Certificate> getCertificate() const;

    [[nodiscard]] uint32_t getVideoSSRC() const;
    [[nodiscard]] uint32_t getRtxVideoSSRC() const;
    [[nodiscard]] uint32_t getAudioSSRC() const;
    [[nodiscard]] uint32_t getRtxAudioSSRC() const;

    [[nodiscard]] std::pair<uint32_t, uint32_t> getVideoSimulastSSRC(const std::string& name) const;

private:
    std::string generateRandomUUID();
    std::string generateRandomString(size_t len);

    RandomGenerator<uint32_t> mRandomGenerator;

    const OfferConfig mConfig;
    const srtc::optional<PubVideoConfig> mVideoConfig;
    const srtc::optional<PubAudioConfig> mAudioConfig;

    const uint64_t mOriginId;

    const uint32_t mVideoSSRC;
    const uint32_t mRtxVideoSSRC;
    const uint32_t mAudioSSRC;
    const uint32_t mRtxAudioSSRC;

    const std::string mVideoMSID;
    const std::string mAudioMSID;

    const std::string mIceUfrag;
    const std::string mIcePassword;

    const std::shared_ptr<X509Certificate> mCert;

    struct LayerSSRC {
        std::string name;
        uint32_t ssrc;
        uint32_t rtx;
    };
    std::vector<LayerSSRC> mLayerSSRC;
};

}

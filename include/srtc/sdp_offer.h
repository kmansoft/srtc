#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "srtc/error.h"
#include "srtc/publish_config.h"
#include "srtc/random_generator.h"
#include "srtc/simulcast_layer.h"
#include "srtc/srtc.h"

namespace srtc
{

class PeerConnection;
class X509Certificate;

struct OfferConfig {
    std::string cname;
    bool enable_rtx = true;
    bool enable_bwe = false;
    bool debug_drop_packets = false;
};

class SdpOffer
{
private:
    friend PeerConnection;

    SdpOffer(const OfferConfig& config,
             const std::optional<PubVideoConfig>& videoConfig,
             const std::optional<PubAudioConfig>& audioConfig);

public:
    ~SdpOffer() = default;

    [[nodiscard]] const OfferConfig& getConfig() const;

    [[nodiscard]] std::pair<std::string, Error> generate();

    [[nodiscard]] std::optional<std::vector<SimulcastLayer>> getVideoSimulcastLayerList() const;
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
    const std::optional<PubVideoConfig> mVideoConfig;
    const std::optional<PubAudioConfig> mAudioConfig;

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

} // namespace srtc

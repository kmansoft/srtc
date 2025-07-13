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
class PeerCandidate;
class SendPacer;
class X509Certificate;

struct OfferConfig {
    std::string cname;
};

struct PubOfferConfig : OfferConfig {
    bool enable_rtx = true;
    bool enable_bwe = false;
    bool debug_drop_packets = false;
};

struct SubOfferConfig : OfferConfig {
    uint16_t pli_interval_millis = 1000;
    uint16_t jitter_buffer_length_millis = 0;
    uint16_t jitter_buffer_nack_delay_millis = 0;
    bool debug_drop_packets = false;
};

class SdpOffer
{
private:
    friend PeerConnection;
    friend PeerCandidate;
    friend SendPacer;

    struct Config : OfferConfig {
        // Common
        bool debug_drop_packets = false;
        // Publish
        bool enable_rtx = true;
        bool enable_bwe = false;
        // Subscribe
        uint16_t pli_interval_millis = 0;
        uint16_t jitter_buffer_length_millis = 0;
        uint16_t jitter_buffer_nack_delay_millis = 0;
    };

    struct VideoCodec {
        Codec codec;
        uint32_t profile_level_id; // for h264

        VideoCodec(Codec codec, uint32_t profile_level_id)
            : codec(codec)
            , profile_level_id(profile_level_id)
        {
        }
    };

    struct VideoConfig {
        std::vector<VideoCodec> codec_list;
        std::vector<SimulcastLayer> simulcast_layer_list;
    };

    struct AudioCodec {
        Codec codec;
        uint32_t minptime;
        bool stereo;

        AudioCodec(Codec codec, uint32_t minptime, bool stereo)
            : codec(codec)
            , minptime(minptime)
            , stereo(stereo)
        {
        }
    };

    struct AudioConfig {
        std::vector<AudioCodec> codec_list;
    };

    SdpOffer(Direction direction,
             const Config& config,
             const std::optional<VideoConfig>& videoConfig,
             const std::optional<AudioConfig>& audioConfig);

public:
    ~SdpOffer() = default;

    [[nodiscard]] Direction getDirection() const;

    [[nodiscard]] const Config& getConfig() const;

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

    const Direction mDirection;
    const Config mConfig;
    const std::optional<VideoConfig> mVideoConfig;
    const std::optional<AudioConfig> mAudioConfig;

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

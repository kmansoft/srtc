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

struct DataChannelConfig {
    std::vector<std::string> data_channels;
};

struct PubOfferConfig {
    std::string cname;
    bool enable_rtx = true;
    bool enable_bwe = false;
    bool enable_rfc8851 = false;
    DataChannelConfig data_channel_config;
};

struct SubOfferConfig {
    std::string cname;
    uint16_t pli_interval_millis = 2000;
    uint16_t jitter_buffer_length_millis = 0;
    uint16_t jitter_buffer_nack_delay_millis = 0;
    DataChannelConfig data_channel_config;
};

class SdpOffer
{
private:
    friend PeerConnection;
    friend PeerCandidate;
    friend SendPacer;

    struct Config {
        // Common
        std::string cname;
        // Data channels
        std::vector<std::string> data_channels;
        // Publish
        bool enable_rtx = true;
        bool enable_bwe = false;
        bool enable_rfc8851 = false;
        // Subscribe
        uint16_t pli_interval_millis = 0;
        uint16_t jitter_buffer_length_millis = 0;
        uint16_t jitter_buffer_nack_delay_millis = 0;
    };

    struct MediaCodec {
        Codec codec;
        uint32_t profile_level_id; // for h264
        uint32_t minptime;         // for audio
        bool stereo;

        MediaCodec(Codec codec, uint32_t profile_level_id, uint32_t minptime, bool stereo)
            : codec(codec)
            , profile_level_id(profile_level_id)
            , minptime(minptime)
            , stereo(stereo)
        {
        }
    };

    struct MediaLine {
        std::string id;
        MediaType mediaType;
        std::vector<MediaCodec> codec_list;
        std::vector<SimulcastLayer> layer_list;
    };

    SdpOffer(Direction direction, const Config& config, const std::vector<MediaLine>& media);

public:
    ~SdpOffer() = default;

    [[nodiscard]] Direction getDirection() const;

    [[nodiscard]] const Config& getConfig() const;

    [[nodiscard]] std::pair<std::string, Error> generate();

    [[nodiscard]] std::string getIceUFrag() const;
    [[nodiscard]] std::string getIcePassword() const;
    [[nodiscard]] std::shared_ptr<X509Certificate> getCertificate() const;

    [[nodiscard]] std::optional<std::vector<SimulcastLayer>> getVideoSimulcastLayerList(
        const std::string& mediaId) const;

    [[nodiscard]] std::pair<uint32_t, uint32_t> getMediaSSRC(const std::string& mediaId) const;
    [[nodiscard]] std::pair<uint32_t, uint32_t> getVideoSimulastSSRC(const std::string& mediaId,
                                                                     const std::string& rid) const;

    [[nodiscard]] bool hasDataChannel() const;
    [[nodiscard]] uint16_t getSctpPort() const;
    [[nodiscard]] uint32_t getSctpMaxMessageSize() const;

private:
    std::string generateRandomUUID();
    std::string generateRandomString(size_t len);

    RandomGenerator<uint32_t> mRandomGenerator;

    const Direction mDirection;
    const Config mConfig;
    const std::vector<MediaLine> mMediaLineList;

    const uint64_t mOriginId;

    const std::string mIceUfrag;
    const std::string mIcePassword;

    const std::shared_ptr<X509Certificate> mCert;

    struct LayerGenerated {
        std::string name;
        uint32_t ssrc = 0;
        uint32_t rtx = 0;

        LayerGenerated(const std::string& name)
            : name(name)
        {
        }
    };

    struct MediaLineGenerated {
        std::string mediaId;
        MediaType mediaType;
        uint32_t ssrc = 0;
        uint32_t rtx = 0;
        std::vector<LayerGenerated> layer;

        MediaLineGenerated(const std::string& mediaId, MediaType mediaType)
            : mediaId(mediaId)
            , mediaType(mediaType)
        {
        }
    };

    std::vector<MediaLineGenerated> mMediaLineGeneratedList;
};

} // namespace srtc

#pragma once

#include "srtc/rtp_extension_source.h"

#include <cstdint>
#include <memory>

namespace srtc {

class Track;
class ByteBuffer;
class Packetizer;
class RtpExtensionBuilder;
class SdpOffer;
class SdpAnswer;

class RtpExtensionSourceTWCC : public RtpExtensionSource {
public:
    RtpExtensionSourceTWCC(
        uint8_t nVideoExtTWCC,
        uint8_t nAudioExtTWCC);
    ~RtpExtensionSourceTWCC() override;

    static std::shared_ptr<RtpExtensionSourceTWCC> factory(
        const std::shared_ptr<SdpOffer>& offer,
        const std::shared_ptr<SdpAnswer>& answer);
  
    [[nodiscard]] bool wants(
        const std::shared_ptr<Track>& track,
        bool isKeyFrame,
        int packetNumber) override;

    void add(
        RtpExtensionBuilder& builder,
        const std::shared_ptr<Track>& track,
        bool isKeyFrame,
        int packetNumber) override;
    
    void updateForRtx(
        RtpExtensionBuilder& builder,
        const std::shared_ptr<Track>& track);

private:
    const uint8_t mVideoExtTWCC;
    const uint8_t mAudioExtTWCC;
    uint16_t mNextPacketSEQ;
};

}

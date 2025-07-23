#pragma once

#include <cstdint>

#include <memory>

namespace srtc
{

class SdpOffer;
class SdpAnswer;
class RtpPacket;
class Track;

class RtpResponderTWCC final
{
public:
    RtpResponderTWCC(uint8_t nVideoExtTWCC, uint8_t nAudioExtTWCC);
    ~RtpResponderTWCC();

    static std::shared_ptr<RtpResponderTWCC> factory(const std::shared_ptr<SdpOffer>& offer,
                                                     const std::shared_ptr<SdpAnswer>& answer);

    void onMediaPacket(const std::shared_ptr<RtpPacket>& packet);

private:
    uint8_t mVideoExtTWCC;
    uint8_t mAudioExtTWCC;

    uint8_t getExtensionId(const std::shared_ptr<Track>& track) const;
};

} // namespace srtc
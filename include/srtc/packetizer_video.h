#pragma once

#include "srtc/packetizer.h"

#include <cstdint>

namespace srtc
{

class Track;
class RtpExtensionSource;
class RtpExtension;

class PacketizerVideo : public Packetizer
{
protected:
    explicit PacketizerVideo(const std::shared_ptr<Track>& track);
    ~PacketizerVideo() override;

    static uint8_t getPadding(const std::shared_ptr<srtc::Track>& track,
                              const std::shared_ptr<srtc::RtpExtensionSource>& simulcast,
                              const std::shared_ptr<srtc::RtpExtensionSource>& twcc,
                              size_t remainingDataSize);

    static srtc::RtpExtension buildExtension(const std::shared_ptr<srtc::Track>& track,
                                             const std::shared_ptr<srtc::RtpExtensionSource>& simulcast,
                                             const std::shared_ptr<srtc::RtpExtensionSource>& twcc,
                                             bool isKeyFrame,
                                             int packetNumber);

    static size_t adjustPacketSize(size_t basicPacketSize, size_t padding, const srtc::RtpExtension& extension);
};

} // namespace srtc

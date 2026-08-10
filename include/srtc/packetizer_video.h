#pragma once

#include "srtc/packetizer.h"

#include <cstdint>
#include <vector>

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

    static uint8_t getPadding(const std::shared_ptr<Track>& track,
                              const std::vector<std::shared_ptr<RtpExtensionSource>>& extensionSourceList,
                              size_t remainingDataSize);

    static size_t adjustPacketSize(size_t basicPacketSize, size_t padding, const RtpExtension& extension);
};

} // namespace srtc

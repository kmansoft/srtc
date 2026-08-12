#pragma once

#include "srtc/packetizer.h"

#include <cstdint>

namespace srtc
{

class Track;
class RtpExtensionSource;
class RtpExtension;

class PacketizerAudio : public Packetizer
{
protected:
    explicit PacketizerAudio(const std::shared_ptr<Track>& track);
    ~PacketizerAudio() override;
};

} // namespace srtc

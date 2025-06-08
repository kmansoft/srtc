#pragma once

#include <memory>
#include <cstdint>

namespace srtc
{

class RtpExtensionBuilder;
class Track;

class RtpExtensionSource
{
public:
    virtual ~RtpExtensionSource();

	[[nodiscard]] virtual uint8_t padding() const = 0;

    [[nodiscard]] virtual bool wants(const std::shared_ptr<Track>& track, bool isKeyFrame, int packetNumber) const = 0;

    virtual void add(RtpExtensionBuilder& builder,
                     const std::shared_ptr<Track>& track,
                     bool isKeyFrame,
                     int packetNumber) = 0;
};

} // namespace srtc
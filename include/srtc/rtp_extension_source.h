#pragma once

#include <memory>

namespace srtc {

class RtpExtensionBuilder;
class Track;

class RtpExtensionSource {
public:
    virtual ~RtpExtensionSource();

    [[nodiscard]] virtual bool wants(
        const std::shared_ptr<Track>& track,
        bool isKeyFrame,
        int packetNumber) = 0;

    virtual void add(
        RtpExtensionBuilder& builder,
        const std::shared_ptr<Track>& track,
        bool isKeyFrame,
        int packetNumber) = 0;
};

}
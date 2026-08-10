#pragma once

#include "rtp_extension.h"
#include "srtc/rtp_extension_source.h"

#include <cstdint>

namespace srtc
{

class SdpAnswer;

class RtpExtensionSourceAbsCaptureTime : public RtpExtensionSource
{
public:
    RtpExtensionSourceAbsCaptureTime();
    ~RtpExtensionSourceAbsCaptureTime() override;

    [[nodiscard]] static std::shared_ptr<RtpExtensionSourceAbsCaptureTime> factory(
        const std::shared_ptr<SdpAnswer>& answer);

    [[nodiscard]] uint8_t getPadding(const std::shared_ptr<Track>& track, size_t remainingDataSize) override;

    void prepare(const std::shared_ptr<Track>& track, uint64_t absCaptureTimeNTP);

    [[nodiscard]] bool wantsExtension(const std::shared_ptr<Track>& track,
                                      bool isKeyFrame,
                                      unsigned int packetNumber) const override;

    void addExtension(RtpExtensionBuilder& builder,
                      const std::shared_ptr<Track>& track,
                      bool isKeyFrame,
                      unsigned int packetNumber) override;

private:
    uint64_t mAbsCaptureTimeNTP;
    uint8_t mExtensionId;
};

} // namespace srtc

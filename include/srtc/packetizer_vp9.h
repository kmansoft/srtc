#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/packetizer_video.h"

namespace srtc
{

class PacketizerVP9 final : public PacketizerVideo
{
public:
    explicit PacketizerVP9(const std::shared_ptr<Track>& track);
    ~PacketizerVP9() override;

    [[nodiscard]] bool isKeyFrame(const ByteBuffer& frame) const override;
    [[nodiscard]] std::vector<std::shared_ptr<RtpPacket>> generate(const std::shared_ptr<RtpExtensionSource>& simulcast,
                                                                   const std::shared_ptr<RtpExtensionSource>& twcc,
                                                                   size_t mediaProtectionOverhead,
                                                                   int64_t pts_usec,
                                                                   const ByteBuffer& frame) override;

private:
    uint16_t mPictureId;
};

} // namespace srtc

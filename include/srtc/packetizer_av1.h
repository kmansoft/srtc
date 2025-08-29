#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/packetizer_video.h"

namespace srtc
{

class PacketizerAV1 final : public PacketizerVideo
{
public:
    explicit PacketizerAV1(const std::shared_ptr<Track>& track);
    ~PacketizerAV1() override;

    [[nodiscard]] bool isKeyFrame(const ByteBuffer& frame) const override;
    [[nodiscard]] std::list<std::shared_ptr<RtpPacket>> generate(const std::shared_ptr<RtpExtensionSource>& simulcast,
                                                                 const std::shared_ptr<RtpExtensionSource>& twcc,
                                                                 size_t mediaProtectionOverhead,
                                                                 int64_t pts_usec,
                                                                 const ByteBuffer& frame) override;
};

} // namespace srtc

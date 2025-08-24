#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/packetizer_video.h"

namespace srtc
{

class PacketizerH264 final : public PacketizerVideo
{
public:
    explicit PacketizerH264(const std::shared_ptr<Track>& track);
    ~PacketizerH264() override;

    void setCodecSpecificData(const std::vector<ByteBuffer>& csd) override;
    [[nodiscard]] bool isKeyFrame(const ByteBuffer& frame) const override;
    std::list<std::shared_ptr<RtpPacket>> generate(const std::shared_ptr<RtpExtensionSource>& simulcast,
                                                   const std::shared_ptr<RtpExtensionSource>& twcc,
                                                   size_t mediaProtectionOverhead,
                                                   int64_t pts_usec,
                                                   const ByteBuffer& frame) override;

private:
    srtc::ByteBuffer mSPS; // Without Annex B header
    srtc::ByteBuffer mPPS;
};

} // namespace srtc

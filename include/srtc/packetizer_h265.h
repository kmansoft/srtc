#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/packetizer_video.h"

namespace srtc
{

class PacketizerH265 final : public PacketizerVideo
{
public:
    explicit PacketizerH265(const std::shared_ptr<Track>& track);
    ~PacketizerH265() override;

    void setCodecSpecificData(const std::vector<ByteBuffer>& csd) override;
    [[nodiscard]] bool isKeyFrame(const ByteBuffer& frame) const override;
    [[nodiscard]] std::list<std::shared_ptr<RtpPacket>> generate(const std::shared_ptr<RtpExtensionSource>& simulcast,
                                                                 const std::shared_ptr<RtpExtensionSource>& twcc,
                                                                 size_t mediaProtectionOverhead,
                                                                 int64_t pts_usec,
                                                                 const ByteBuffer& frame) override;

private:
    ByteBuffer mVPS; // Without Annex B header
    ByteBuffer mSPS;
    ByteBuffer mPPS;
};

} // namespace srtc

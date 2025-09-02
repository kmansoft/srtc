#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/depacketizer_video.h"

namespace srtc
{

class DepacketizerH264 final : public DepacketizerVideo
{
public:
    explicit DepacketizerH264(const std::shared_ptr<Track>& track);
    ~DepacketizerH264() override;

    void reset() override;

    void extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList) override;

protected:
    [[nodiscard]] bool isFrameStart(const ByteBuffer& payload) const override;

private:
    uint8_t mHaveBits;
    ByteBuffer mFrameBuffer;
    uint64_t mLastRtpTimestamp;

    void extractImpl(std::vector<ByteBuffer>& out, const JitterBufferItem* packet, ByteBuffer&& nalu);
};

} // namespace srtc
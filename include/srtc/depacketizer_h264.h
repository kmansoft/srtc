#pragma once

#include "srtc/depacketizer.h"
#include "srtc/byte_buffer.h"

namespace srtc
{

class DepacketizerH264 final : public Depacketizer
{
public:
    explicit DepacketizerH264(const std::shared_ptr<Track>& track);
    ~DepacketizerH264() override;

    [[nodiscard]] PacketKind getPacketKind(const ByteBuffer& packet) override;

    void reset() override;

    void extract(std::vector<ByteBuffer>& out, const JitterBufferItem* packet) override;
    void extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList) override;

private:
    uint8_t mHaveBits;
    ByteBuffer mFrameBuffer;
    uint64_t mLastRtpTimestamp;

    void extractImpl(std::vector<ByteBuffer>& out, const JitterBufferItem* packet, ByteBuffer&& frame);
};

} // namespace srtc
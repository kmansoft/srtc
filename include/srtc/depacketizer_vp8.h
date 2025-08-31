#pragma once

#include "srtc/depacketizer_video.h"

namespace srtc
{

class DepacketizerVP8 final : public DepacketizerVideo
{
public:
    explicit DepacketizerVP8(const std::shared_ptr<Track>& track);
    ~DepacketizerVP8() override;

    [[nodiscard]] PacketKind getPacketKind(const ByteBuffer& payload, bool marker) const override;

    void reset() override;

    void extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList) override;

private:
    bool mSeenKeyFrame;

    void extractImpl(std::vector<ByteBuffer>& out, const JitterBufferItem* packet, ByteBuffer&& frame);
};

} // namespace srtc
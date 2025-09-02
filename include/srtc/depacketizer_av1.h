#pragma once

#include "srtc/depacketizer_video.h"

namespace srtc
{

class DepacketizerAV1 final : public DepacketizerVideo
{
public:
    explicit DepacketizerAV1(const std::shared_ptr<Track>& track);
    ~DepacketizerAV1() override;

    void reset() override;

    void extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList) override;

protected:
    [[nodiscard]] bool isFrameStart(const ByteBuffer& payload) const override;

private:
    bool mSeenNewSequence;

    void extractImpl(std::vector<ByteBuffer>& out, const JitterBufferItem* packet, ByteBuffer&& frame);
};

} // namespace srtc
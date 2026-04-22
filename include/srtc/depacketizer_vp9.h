#pragma once

#include "srtc/depacketizer_video.h"

namespace srtc
{

class DepacketizerVP9 final : public DepacketizerVideo
{
public:
    explicit DepacketizerVP9(const std::shared_ptr<Track>& track);
    ~DepacketizerVP9() override;

    void reset() override;

    void extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList) override;

protected:
    [[nodiscard]] bool isFrameStart(const ByteBuffer& payload) const override;

private:
    bool mSeenKeyFrame;
};

} // namespace srtc

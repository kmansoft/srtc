#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/depacketizer.h"

namespace srtc
{

class DepacketizerVideo : public Depacketizer
{
public:
    explicit DepacketizerVideo(const std::shared_ptr<Track>& track);
    ~DepacketizerVideo() override;

//     [[nodiscard]] PacketKind getPacketKind(const ByteBuffer& payload, bool marker) const final;
//
// protected:
//     [[nodiscard]] virtual bool isFrameStart(const ByteBuffer& payload) const = 0;
};
} // namespace srtc
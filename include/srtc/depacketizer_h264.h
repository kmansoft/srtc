#pragma once

#include "srtc/depacketizer.h"

namespace srtc
{

class DepacketizerH264 final : public Depacketizer
{
public:
    explicit DepacketizerH264(const std::shared_ptr<Track>& track);
    ~DepacketizerH264() override;

    [[nodiscard]] PacketKind getPacketKind(const ByteBuffer& packet) override;

    void reset() override;

    void extract(std::vector<ByteBuffer>& out, ByteBuffer& packet) override;
    void extract(std::vector<ByteBuffer>& out, const std::vector<ByteBuffer*>& packetList) override;

private:
    uint8_t mHaveBits;

    void extractImpl(std::vector<ByteBuffer>& out, ByteBuffer&& frame);
};

} // namespace srtc
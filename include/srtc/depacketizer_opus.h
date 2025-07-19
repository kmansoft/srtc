#pragma once

#include "srtc/depacketizer.h"

namespace srtc
{

class DepacketizerOpus final : public Depacketizer
{
public:
    explicit DepacketizerOpus(const std::shared_ptr<Track>& track);
    ~DepacketizerOpus() override;

    [[nodiscard]] PacketKind getPacketKind(const ByteBuffer& packet) override;
    void extract(std::vector<ByteBuffer>& out, ByteBuffer& packet) override;
    void extract(std::vector<ByteBuffer>& out, const std::vector<ByteBuffer*>& packetList) override;
};

} // namespace srtc
#pragma once

#include "srtc/depacketizer.h"

namespace srtc
{

class DepacketizerOpus final : public Depacketizer
{
public:
    explicit DepacketizerOpus(const std::shared_ptr<Track>& track);
    ~DepacketizerOpus() override;

    [[nodiscard]] PacketKind getPacketKind(const ByteBuffer& payload, bool marker) const override;

    void reset() override;

    void extract(std::vector<ByteBuffer>& out, const JitterBufferItem* packet) override;
    void extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList) override;
};

} // namespace srtc
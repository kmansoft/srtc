#include "srtc/depacketizer_opus.h"

#include <cassert>

namespace srtc
{

DepacketizerOpus::DepacketizerOpus(const std::shared_ptr<Track>& track)
    : Depacketizer(track)
{
}
DepacketizerOpus::~DepacketizerOpus() = default;

PacketKind DepacketizerOpus::getPacketKind(const JitterBufferItem* packet) const
{
    return PacketKind::Standalone;
}

void DepacketizerOpus::reset()
{
    // Nothing
}

void DepacketizerOpus::extract(std::vector<ByteBuffer>& out, const JitterBufferItem* packet)
{
    assert(getPacketKind(packet) == PacketKind::Standalone);

    out.clear();

    if (!packet->payload.empty()) {
        out.emplace_back(packet->payload.copy());
    }
}

void DepacketizerOpus::extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList)
{
    // Opus packets are always standalone
    out.clear();
    assert(false);
}

} // namespace srtc
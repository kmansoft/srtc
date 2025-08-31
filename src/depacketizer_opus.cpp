#include "srtc/depacketizer_opus.h"

#include <cassert>

namespace srtc
{

DepacketizerOpus::DepacketizerOpus(const std::shared_ptr<Track>& track)
    : Depacketizer(track)
{
}
DepacketizerOpus::~DepacketizerOpus() = default;

PacketKind DepacketizerOpus::getPacketKind(const ByteBuffer& payload, bool marker) const
{
    return PacketKind::Standalone;
}

void DepacketizerOpus::reset()
{
    // Nothing
}

void DepacketizerOpus::extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList)
{
    out.clear();
    assert(packetList.size() == 1);

    const auto packet = packetList[0];
    assert(getPacketKind(packet->payload, packet->marker) == PacketKind::Standalone);

    if (!packet->payload.empty()) {
        out.emplace_back(packet->payload.copy());
    }
}

} // namespace srtc
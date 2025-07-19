#include "srtc/depacketizer_opus.h"
#include "srtc/byte_buffer.h"

#include <cassert>

namespace srtc
{

DepacketizerOpus::DepacketizerOpus(const std::shared_ptr<Track>& track)
	: Depacketizer(track)
{
}
DepacketizerOpus::~DepacketizerOpus() = default;

PacketKind DepacketizerOpus::getPacketKind(const ByteBuffer& packet)
{
	return PacketKind::Standalone;
}

void DepacketizerOpus::extract(std::vector<ByteBuffer>& out, ByteBuffer& packet)
{
	assert(getPacketKind(packet) == PacketKind::Standalone);

    out.clear();
    out.emplace_back(std::move(packet));
}

void DepacketizerOpus::extract(std::vector<ByteBuffer>& out, const std::vector<ByteBuffer*>& packetList)
{
	// Opus packets are always standalone
    out.clear();
	assert(false);
}

} // namespace srtc
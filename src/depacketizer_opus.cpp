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

std::list<ByteBuffer> DepacketizerOpus::extract(ByteBuffer& packet)
{
	std::list<ByteBuffer> list;

	assert(getPacketKind(packet) == PacketKind::Standalone);

	list.emplace_back(std::move(packet));

	return list;
}

std::list<ByteBuffer> DepacketizerOpus::extract(const std::vector<ByteBuffer*>& packetList)
{
	// Opus packets are always standalone
	assert(false);
	return {};
}

} // namespace srtc
#pragma once

#include "srtc/depacketizer.h"

namespace srtc {

class DepacketizerOpus final : public Depacketizer
{
public:
	explicit DepacketizerOpus(const std::shared_ptr<Track>& track);
	~DepacketizerOpus() override;

	[[nodiscard]] PacketKind getPacketKind(const ByteBuffer& packet) override;
	[[nodiscard]] std::list<ByteBuffer> extract(ByteBuffer& packet) override;
	[[nodiscard]] std::list<ByteBuffer> extract(const std::vector<ByteBuffer*>& packetList) override;
};

}
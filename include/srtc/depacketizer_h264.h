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
	[[nodiscard]] std::list<ByteBuffer> extract(ByteBuffer& packet) override;
	[[nodiscard]] std::list<ByteBuffer> extract(const std::vector<ByteBuffer*>& packetList) override;
};

} // namespace srtc
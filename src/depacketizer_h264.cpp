#include "srtc/depacketizer_h264.h"
#include "srtc/byte_buffer.h"
#include "srtc/h264.h"
#include "srtc/util.h"

#include <cassert>

namespace srtc
{

DepacketizerH264::DepacketizerH264(const std::shared_ptr<Track>& track)
	: Depacketizer(track)
{
}

DepacketizerH264::~DepacketizerH264()
{
}

PacketKind DepacketizerH264::getPacketKind(const ByteBuffer& packet)
{
	ByteReader reader(packet);
	if (reader.remaining() >= 1) {
		// https://datatracker.ietf.org/doc/html/rfc6184#section-5.4
		const auto value = reader.readU8();
		const auto type = value & 0x1F;

		if (type == h264::STAP_A) {
			// https://datatracker.ietf.org/doc/html/rfc6184#section-5.7.1
			return PacketKind::Standalone;
		} else if (type == h264::FU_A) {
			// https://datatracker.ietf.org/doc/html/rfc6184#section-5.8
			if (reader.remaining() >= 1) {
				const auto header = reader.readU8();
				if ((header & (1 << 7)) != 0) {
					return PacketKind::Start;
				} else if ((header & (1 << 6)) != 0) {
					return PacketKind::End;
				} else {
					return PacketKind::Middle;
				}
			}
		} else if (type >= 1 && type <= 23) {
			// https://datatracker.ietf.org/doc/html/rfc6184#section-5.4
			return PacketKind::Standalone;
		}
	}

	return PacketKind::Standalone;
}

void DepacketizerH264::extract(std::vector<ByteBuffer>& out, ByteBuffer& packet)
{
	out.clear();

	ByteReader reader(packet);
	if (reader.remaining() >= 1) {
		// https://datatracker.ietf.org/doc/html/rfc6184#section-5.4
		const auto value = reader.readU8();
		const auto type = value & 0x1F;

		if (type == h264::STAP_A) {
			// https://datatracker.ietf.org/doc/html/rfc6184#section-5.7.1
			while (reader.remaining() >= 2) {
				const auto size = reader.readU16();
				if (reader.remaining() < size) {
					break;
				}

				ByteBuffer  buf(packet.data() + reader.position(), size);
                out.emplace_back(std::move(buf));

				reader.skip(size);
			}
		} else {
			// https://datatracker.ietf.org/doc/html/rfc6184#section-5.4
            out.emplace_back(std::move(packet));
		}
	}
}

void DepacketizerH264::extract(std::vector<ByteBuffer>& out, const std::vector<ByteBuffer*>& packetList)
{
    out.clear();

#ifndef NDEBUG
	assert(!packetList.empty());
	assert(getPacketKind(*packetList[0]) == PacketKind::Start);
	for (size_t i = 1; i < packetList.size() - 1; i += 1) {
		assert(getPacketKind(*packetList[i]) == PacketKind::Middle);
	}
	assert(getPacketKind(*packetList[packetList.size() - 1]) == PacketKind::End);
#endif

	ByteBuffer buf;
	ByteWriter w(buf);

	for (const auto packet : packetList) {
		ByteReader reader(*packet);

		if (reader.remaining() > 2) {
			const auto indicator = reader.readU8();
			const auto header = reader.readU8();

			const auto nri = indicator & 0x60;
			const auto type = header & 0x1F;

			if (buf.empty()) {
				w.writeU8(nri | type);
			}

			const auto pos = reader.position();
			w.write(packet->data() + pos, packet->size() - pos);
		}
	}

	out.emplace_back(std::move(buf));
}

} // namespace srtc
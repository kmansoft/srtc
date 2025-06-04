#include "srtc/rtcp_packet.h"
#include "srtc/track.h"

namespace srtc
{

RtcpPacket::RtcpPacket(const std::shared_ptr<Track>& track, uint8_t rc, uint8_t payloadId, ByteBuffer&& payload)
    : mTrack(track)
    , mSSRC(track->getSSRC())
    , mRC(rc)
    , mPayloadId(payloadId)
    , mPayload(std::move(payload))
{
}

RtcpPacket::~RtcpPacket() = default;

std::shared_ptr<Track> RtcpPacket::getTrack() const
{
    return mTrack;
}

uint8_t RtcpPacket::getRC() const
{
    return mRC;
}

uint8_t RtcpPacket::getPayloadId() const
{
    return mPayloadId;
}

uint32_t RtcpPacket::getSSRC() const
{
    return mSSRC;
}

ByteBuffer RtcpPacket::generate() const
{
    ByteBuffer buf;
    ByteWriter w(buf);

    // https://en.wikipedia.org/wiki/RTP_Control_Protocol

    const uint16_t header = (2 << 14) | ((mRC & 0x1F) << 8) | (mPayloadId);

    const auto lenRaw = 2 + 2 + 4 + mPayload.size();
    const auto lenRTCP = (lenRaw + 3) / 4 - 1;

	std::printf("RTCP generate, len_raw = %zu, len_rtcp = %zu\n", lenRaw, lenRTCP);

    w.writeU16(header);
    w.writeU16(lenRTCP);
    w.writeU32(mSSRC);

    w.write(mPayload);

    for (auto padding = lenRaw; padding % 4 != 0; padding += 1) {
        w.writeU8(0);
    }

    return buf;
}

} // namespace srtc
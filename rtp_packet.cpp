#include "srtc/rtp_packet.h"

#include <utility>

namespace srtc {

RtpPacket::RtpPacket(bool marker,
                     uint8_t payloadType,
                     uint16_t sequence,
                     uint32_t timestamp,
                     uint32_t ssrc,
                     srtc::ByteBuffer& payload)
    : mMarker(marker)
    , mPayloadType(payloadType)
    , mSequence(sequence)
    , mTimestamp(timestamp)
    , mSSRC(ssrc)
    , mPayload(std::move(payload))
{
}

RtpPacket::~RtpPacket() = default;

ByteBuffer RtpPacket::generate() const
{
    // https://blog.webex.com/engineering/introducing-rtp-the-packet-format/

    ByteBuffer buf;
    ByteWriter writer(buf);

    // V=1 | P | X | CC | M | PT
    const uint16_t header = (1 << 15) | (mMarker ? (1 << 7) : 0) | (mPayloadType & 0x7F);
    writer.writeU16(header);

    writer.writeU16(mSequence);
    writer.writeU32(mTimestamp);
    writer.writeU32(mSSRC);

    // Payload
    buf.append(mPayload);

    return buf;
}

}

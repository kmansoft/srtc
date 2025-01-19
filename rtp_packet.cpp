#include "srtc/rtp_packet.h"
#include "srtc/track.h"

#include <utility>

namespace srtc {

RtpPacket::RtpPacket(const std::shared_ptr<Track>& track,
                     bool marker,
                     uint16_t sequence,
                     uint32_t timestamp,
                     srtc::ByteBuffer&& payload)
    : mTrack(track)
    , mMarker(marker)
    , mPayloadType(track->getPayloadType())
    , mSequence(sequence)
    , mTimestamp(timestamp)
    , mSSRC(track->getSSRC())
    , mPayload(std::move(payload))
{
}

RtpPacket::~RtpPacket() = default;

std::shared_ptr<Track> RtpPacket::getTrack() const
{
    return mTrack;
}

uint8_t RtpPacket::getPayloadType() const
{
    return mPayloadType;
}

uint16_t RtpPacket::getSequence() const
{
    return mSequence;
}

uint32_t RtpPacket::getSSRC() const
{
    return mSSRC;
}

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

#include "srtc/rtp_packet.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/track.h"

#include <cassert>
#include <utility>

namespace srtc {

RtpPacket::RtpPacket(const std::shared_ptr<Track>& track,
                     bool marker,
                     uint32_t rollover,
                     uint16_t sequence,
                     uint32_t timestamp,
                     srtc::ByteBuffer&& payload)
    : mTrack(track)
    , mSSRC(track->getSSRC())
    , mPayloadId(track->getPayloadId())
    , mMarker(marker)
    , mRollover(rollover)
    , mSequence(sequence)
    , mTimestamp(timestamp)
    , mPayload(std::move(payload))
{
}

RtpPacket::~RtpPacket() = default;

std::shared_ptr<Track> RtpPacket::getTrack() const
{
    return mTrack;
}

uint8_t RtpPacket::getPayloadId() const
{
    return mPayloadId;
}

uint16_t RtpPacket::getSequence() const
{
    return mSequence;
}

RtpPacket::Output RtpPacket::generate() const
{
    // https://blog.webex.com/engineering/introducing-rtp-the-packet-format/

    ByteBuffer buf;
    ByteWriter writer(buf);

    // V=1 | P | X | CC | M | PT
    const uint16_t header = (1 << 15) | (mMarker ? (1 << 7) : 0) | (mPayloadId & 0x7F);
    writer.writeU16(header);

    writer.writeU16(mSequence);
    writer.writeU32(mTimestamp);
    writer.writeU32(mSSRC);

    // Payload
    writer.write(mPayload);

    return { mRollover, std::move(buf) };
}

uint32_t RtpPacket::getSSRC() const
{
    return mSSRC;
}

RtpPacket::Output RtpPacket::generateRtx() const
{
    const auto rtxPayloadId = mTrack->getRtxPayloadId();
    assert(rtxPayloadId > 0);

    // https://datatracker.ietf.org/doc/html/rfc4588#section-4

    ByteBuffer buf;
    ByteWriter writer(buf);

    // V=1 | P | X | CC | M | PT
    const uint16_t header = (1 << 15) | (mMarker ? (1 << 7) : 0) | (rtxPayloadId & 0x7F);
    writer.writeU16(header);

    const auto packetSource = mTrack->getRtxPacketSource();
    const auto [ rtxRollover, rtxSequence ] = packetSource->getNextSequence();
    writer.writeU16(rtxSequence);
    writer.writeU32(mTimestamp);
    writer.writeU32(mTrack->getRtxSSRC());

    // Payload
    writer.writeU16(mSequence);
    writer.write(mPayload);

    return { rtxRollover, std::move(buf) };
}

}

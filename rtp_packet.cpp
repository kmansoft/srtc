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
                     ByteBuffer&& payload)
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

RtpPacket::RtpPacket(const std::shared_ptr<Track>& track,
                     bool marker,
                     uint32_t rollover,
                     uint16_t sequence,
                     uint32_t timestamp,
                     RtpExtension&& extension,
                     ByteBuffer&& payload)
        : mTrack(track)
        , mSSRC(track->getSSRC())
        , mPayloadId(track->getPayloadId())
        , mMarker(marker)
        , mRollover(rollover)
        , mSequence(sequence)
        , mTimestamp(timestamp)
        , mExtension(std::move(extension))
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

uint32_t RtpPacket::getRollover() const
{
    return mRollover;
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

    // V=2 | P | X | CC | M | PT
    const auto extension = !mExtension.empty();
    const uint16_t header = (2 << 14)
            | (extension ? (1 << 12) : 0)
            | (mMarker ? (1 << 7) : 0)
            | (mPayloadId & 0x7F);
    writer.writeU16(header);

    writer.writeU16(mSequence);
    writer.writeU32(mTimestamp);
    writer.writeU32(mSSRC);

    // Extension
    writeExtension(writer);

    // Payload
    writePayload(writer);

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

    // V=2 | P | X | CC | M | PT
    const auto extension = !mExtension.empty();
    const uint16_t header = (2 << 14)
            | (extension ? (1 << 12) : 0)
            | (mMarker ? (1 << 7) : 0)
            | (rtxPayloadId & 0x7F);
    writer.writeU16(header);

    const auto packetSource = mTrack->getRtxPacketSource();
    const auto [ rtxRollover, rtxSequence ] = packetSource->getNextSequence();
    writer.writeU16(rtxSequence);
    writer.writeU32(mTimestamp);
    writer.writeU32(mTrack->getRtxSSRC());

    // The original sequence
    writer.writeU16(mSequence);

    // Extension
    writeExtension(writer);

    // Payload
    writePayload(writer);

    return { rtxRollover, std::move(buf) };
}

void RtpPacket::writeExtension(ByteWriter& writer) const
{
    if (mExtension.empty()) {
        return;
    }

    // https://datatracker.ietf.org/doc/html/rfc3550#section-5.3.1
    writer.writeU16(mExtension.getId());

    const auto& extensionData = mExtension.getData();
    const auto extensionSize = extensionData.size();
    writer.writeU16((extensionSize + 3) / 4);

    writer.write(extensionData);

    for (size_t padding = extensionSize; padding % 4 != 0; padding += 1) {
        writer.writeU8(0);
    }
}

void RtpPacket::writePayload(ByteWriter& writer) const
{
    writer.write(mPayload);
}

}

#include "srtc/rtp_packet.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/track.h"

#include <cassert>
#include <utility>

namespace {

void writeExtension(srtc::ByteWriter& writer, const srtc::RtpExtension& extension)
{
    if (extension.empty()) {
        return;
    }

    // https://datatracker.ietf.org/doc/html/rfc3550#section-5.3.1
    writer.writeU16(extension.getId());

    const auto& extensionData = extension.getData();
    const auto extensionSize = extensionData.size();
    writer.writeU16((extensionSize + 3) / 4);

    writer.write(extensionData);

    for (size_t padding = extensionSize; padding % 4 != 0; padding += 1) {
        writer.writeU8(0);
    }
}

void writePayload(srtc::ByteWriter& writer, const srtc::ByteBuffer& payload)
{
    writer.write(payload);
}

}

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

RtpExtension RtpPacket::getExtension() const
{
    return mExtension.copy();
}

uint8_t RtpPacket::getPayloadId() const
{
    return mPayloadId;
}

uint32_t RtpPacket::getRollover() const
{
    return mRollover;
}

size_t RtpPacket::getPayloadSize() const
{
    return mPayload.size();
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
    const auto ext = !mExtension.empty();
    const uint16_t header = (2 << 14)
            | (ext ? (1 << 12) : 0)
            | (mMarker ? (1 << 7) : 0)
            | (mPayloadId & 0x7F);
    writer.writeU16(header);

    writer.writeU16(mSequence);
    writer.writeU32(mTimestamp);
    writer.writeU32(mSSRC);

    // Extension
    writeExtension(writer, mExtension);

    // Payload
    writePayload(writer, mPayload);

    return { std::move(buf), mRollover };
}

uint32_t RtpPacket::getSSRC() const
{
    return mSSRC;
}

RtpPacket::Output RtpPacket::generateRtx(const RtpExtension& extension) const
{
    const auto rtxPayloadId = mTrack->getRtxPayloadId();
    assert(rtxPayloadId > 0);

    // https://datatracker.ietf.org/doc/html/rfc4588#section-4

    ByteBuffer buf;
    ByteWriter writer(buf);

    // V=2 | P | X | CC | M | PT
    const auto ext = !extension.empty();
    const uint16_t header = (2 << 14)
            | (ext ? (1 << 12) : 0)
            | (mMarker ? (1 << 7) : 0)
            | (rtxPayloadId & 0x7F);
    writer.writeU16(header);

    const auto packetSource = mTrack->getRtxPacketSource();
    const auto [ rtxRollover, rtxSequence ] = packetSource->getNextSequence();
    writer.writeU16(rtxSequence);
    writer.writeU32(mTimestamp);
    writer.writeU32(mTrack->getRtxSSRC());

    // Extension
    writeExtension(writer, extension);

    // The original sequence
    writer.writeU16(mSequence);

    // Payload
    writePayload(writer, mPayload);

    return { std::move(buf), rtxRollover };
}


}

#include "srtc/packetizer_vp8.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/rtp_time_source.h"
#include "srtc/track.h"

namespace srtc
{

PacketizerVP8::PacketizerVP8(const std::shared_ptr<Track>& track)
    : PacketizerVideo(track)
{
}

PacketizerVP8::~PacketizerVP8() = default;

bool PacketizerVP8::isKeyFrame(const srtc::ByteBuffer& frame) const
{
    const auto frameData = frame.data();
    const auto frameSize = frame.size();
    if (frameSize < 3) {
        return false;
    }

    const auto tag = frameData[0] | (frameData[1] << 8) | (frameData[2] << 16);
    const auto tagFrameType = tag & 0x01;

    return tagFrameType == 0;
}

std::list<std::shared_ptr<RtpPacket>> PacketizerVP8::generate(const std::shared_ptr<RtpExtensionSource>& simulcast,
                                                              const std::shared_ptr<RtpExtensionSource>& twcc,
                                                              size_t mediaProtectionOverhead,
                                                              int64_t pts_usec,
                                                              const ByteBuffer& frame)
{
    std::list<std::shared_ptr<RtpPacket>> result;

    // https://datatracker.ietf.org/doc/html/rfc7741

    const auto track = getTrack();

    const auto timeSource = track->getRtpTimeSource();
    const auto packetSource = track->getRtpPacketSource();

    const auto frameTimestamp = timeSource->getFrameTimestamp(pts_usec);

    // https://datatracker.ietf.org/doc/html/rfc6386#section-9.1
    const auto frameData = frame.data();
    const auto frameSize = frame.size();
    if (frameSize < 3) {
        return result;
    }

    const auto tag = frameData[0] | (frameData[1] << 8) | (frameData[2] << 16);
    const auto tagFrameType = tag & 0x01;

    // https://datatracker.ietf.org/doc/html/rfc7741#section-4.2
    auto dataPtr = frame.data();
    auto dataSize = frame.size();

    const auto basicPacketSize = getBasicPacketSize(mediaProtectionOverhead);

    auto packetNumber = 0;
    while (dataSize > 0) {
        const auto [rollover, sequence] = packetSource->getNextSequence();

        auto padding = getPadding(track, simulcast, twcc, dataSize);
        RtpExtension extension = buildExtension(track, simulcast, twcc, tagFrameType == 0, packetNumber);

        // The "-1" is for VP8 payload descriptor
        const auto packetSize = adjustPacketSize(basicPacketSize - 1, padding, extension);

        ByteBuffer payload;
        ByteWriter writer(payload);

        // |X|R|N|S|R| PID |
        writer.writeU8((tagFrameType << 5) | ((packetNumber == 0 ? 1 : 0) << 4));

        // Payload
        const auto writeNow = std::min(dataSize, packetSize);
        writer.write(dataPtr, writeNow);

        // Make a packet
        const auto marker = dataSize <= packetSize;
        result.push_back(extension.empty()
                             ? std::make_shared<RtpPacket>(
                                   track, marker, rollover, sequence, frameTimestamp, padding, std::move(payload))
                             : std::make_shared<RtpPacket>(track,
                                                           marker,
                                                           rollover,
                                                           sequence,
                                                           frameTimestamp,
                                                           padding,
                                                           std::move(extension),
                                                           std::move(payload)));

        // Advance
        dataPtr += writeNow;
        dataSize -= writeNow;
        packetNumber += 1;
    }

    return result;
}

} // namespace srtc

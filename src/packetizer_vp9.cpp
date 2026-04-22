#include "srtc/packetizer_vp9.h"
#include "srtc/codec_vp9.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/rtp_time_source.h"
#include "srtc/track.h"

#include <cassert>

namespace srtc
{

PacketizerVP9::PacketizerVP9(const std::shared_ptr<Track>& track)
    : PacketizerVideo(track)
    , mPictureId(0)
{
    assert(track->getCodec() == Codec::VP9);
}

PacketizerVP9::~PacketizerVP9() = default;

bool PacketizerVP9::isKeyFrame(const ByteBuffer& frame) const
{
    return srtc::vp9::isKeyFrame(frame.data(), frame.size());
}

std::vector<std::shared_ptr<RtpPacket>> PacketizerVP9::generate(const std::shared_ptr<RtpExtensionSource>& simulcast,
                                                                const std::shared_ptr<RtpExtensionSource>& twcc,
                                                                size_t mediaProtectionOverhead,
                                                                int64_t pts_usec,
                                                                const ByteBuffer& frame)
{
    std::vector<std::shared_ptr<RtpPacket>> result;

    // https://www.rfc-editor.org/rfc/rfc9628

    const auto track = getTrack();

    const auto timeSource = track->getRtpTimeSource();
    const auto packetSource = track->getRtpPacketSource();

    const auto frameTimestamp = timeSource->getFrameTimestamp(pts_usec);
    const auto frameIsKeyFrame = srtc::vp9::isKeyFrame(frame.data(), frame.size());

    // Claim and advance the picture ID (15-bit, wraps)
    const auto pictureId = mPictureId;
    mPictureId = (mPictureId + 1) & 0x7FFF;

    // https://www.rfc-editor.org/rfc/rfc9628#section-4
    // Descriptor is always 3 bytes: flag byte + 2-byte picture ID (M=1)
    constexpr size_t kDescriptorSize = 3;

    const auto basicPacketSize = getBasicPacketSize(mediaProtectionOverhead);

    auto currData = frame.data();
    auto currSize = frame.size();
    auto packetNumber = 0u;

    while (currSize > 0) {
        const auto [rollover, sequence] = packetSource->getNextSequence();

        const auto padding = getPadding(track, simulcast, twcc, currSize);
        RtpExtension extension = buildExtension(track, simulcast, twcc, frameIsKeyFrame, packetNumber);

        // "-kDescriptorSize" reserves room for the payload descriptor
        const auto packetSize = adjustPacketSize(basicPacketSize - kDescriptorSize, padding, extension);

        const bool startOfFrame = (packetNumber == 0);
        const bool endOfFrame = (currSize <= packetSize);

        ByteBuffer payload;
        ByteWriter writer(payload);

        // Payload descriptor
        uint8_t descBuf[3];
        const auto descSize = srtc::vp9::buildPayloadDescriptor(
            descBuf, sizeof(descBuf), startOfFrame, endOfFrame, !frameIsKeyFrame, pictureId);
        writer.write(descBuf, descSize);

        // VP9 bitstream fragment
        const auto writeNow = std::min(currSize, packetSize);
        writer.write(currData, writeNow);

        const auto marker = endOfFrame;
        result.push_back(std::make_shared<RtpPacket>(
            track, marker, rollover, sequence, frameTimestamp, padding, std::move(extension), std::move(payload)));

        currData += writeNow;
        currSize -= writeNow;
        packetNumber += 1;
    }

    return result;
}

} // namespace srtc

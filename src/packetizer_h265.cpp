#include "srtc/packetizer_h265.h"
#include "srtc/codec_h265.h"
#include "srtc/logging.h"
#include "srtc/rtp_extension.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_extension_source.h"
#include "srtc/rtp_packet.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/rtp_time_source.h"
#include "srtc/track.h"

#include <cassert>
#include <list>

#define LOG(level, ...) srtc::log(level, "Packetizer_h265", __VA_ARGS__)

namespace srtc
{
using namespace h265;

PacketizerH265::PacketizerH265(const std::shared_ptr<Track>& track)
    : PacketizerVideo(track)
{
    assert(track->getCodec() == Codec::H265);
}

PacketizerH265::~PacketizerH265() = default;

void PacketizerH265::setCodecSpecificData(const std::vector<ByteBuffer>& csd)
{
    mVPS.clear();
    mSPS.clear();
    mPPS.clear();

    for (const auto& item : csd) {
        for (NaluParser parser(item); parser; parser.next()) {
            switch (parser.currType()) {
            case NaluType::VPS:
                mVPS.assign(parser.currData(), parser.currDataSize());
                break;
            case NaluType::SPS:
                mSPS.assign(parser.currData(), parser.currDataSize());
                break;
            case NaluType::PPS:
                mPPS.assign(parser.currData(), parser.currDataSize());
                break;
            default:
                break;
            }
        }
    }

    if (mVPS.empty() || mSPS.empty() || mPPS.empty()) {
        LOG(SRTC_LOG_E, "Could not extract VPS, SPS, and PPS from codec specific data");
    }
}

bool PacketizerH265::isKeyFrame(const ByteBuffer& frame) const
{
    for (NaluParser parser(frame); parser; parser.next()) {
        const auto naluType = parser.currType();
        if (isKeyFrameNalu(naluType)) {
            return true;
        }
    }

    return false;
}

std::list<std::shared_ptr<RtpPacket>> PacketizerH265::generate(const std::shared_ptr<RtpExtensionSource>& simulcast,
                                                               const std::shared_ptr<RtpExtensionSource>& twcc,
                                                               size_t mediaProtectionOverhead,
                                                               int64_t pts_usec,
                                                               const ByteBuffer& frame)
{
    std::list<std::shared_ptr<RtpPacket>> result;

    // https://datatracker.ietf.org/doc/html/rfc7798

    bool addedParameters = false;

    const auto track = getTrack();

    const auto timeSource = track->getRtpTimeSource();
    const auto packetSource = track->getRtpPacketSource();

    const auto frameTimestamp = timeSource->getFrameTimestamp(pts_usec);

    for (NaluParser parser(frame); parser; parser.next()) {
        const auto naluType = parser.currType();

        if (naluType == NaluType::VPS) {
            // Update VPS
            mVPS.assign(parser.currData(), parser.currDataSize());
        } else if (naluType == NaluType::SPS) {
            // Update SPS
            mSPS.assign(parser.currData(), parser.currDataSize());
        } else if (naluType == NaluType::PPS) {
            // Update PPS
            mPPS.assign(parser.currData(), parser.currDataSize());
        } else if (isKeyFrameNalu(naluType)) {
            // Send codec-specific data first as an AP
            // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
            if (!addedParameters && !mVPS.empty() && !mSPS.empty() && !mPPS.empty()) {
                ByteBuffer payload;
                ByteWriter writer(payload);

                // https://datatracker.ietf.org/doc/html/rfc7798#section-1.1.4
                writer.writeU8(kPacket_AP << 1);
                writer.writeU8(0);

                // VPS
                writer.writeU16(static_cast<uint16_t>(mVPS.size()));
                writer.write(mVPS);

                // SPS
                writer.writeU16(static_cast<uint16_t>(mSPS.size()));
                writer.write(mSPS);

                // PPS
                writer.writeU16(static_cast<uint16_t>(mPPS.size()));
                writer.write(mPPS);

                RtpExtension extension = buildExtension(track, simulcast, twcc, true, 0);

                const auto [rollover, sequence] = packetSource->getNextSequence();
                result.push_back(std::make_shared<RtpPacket>(
                    track, false, rollover, sequence, frameTimestamp, 0, std::move(extension), std::move(payload)));
            }

            addedParameters = true;
        }

        if (!isParameterNalu(naluType)) {
            // Now the frame itself
            const auto naluDataPtr = parser.currData();
            const auto naluDataSize = parser.currDataSize();

            uint8_t padding = getPadding(track, simulcast, twcc, naluDataSize);
            RtpExtension extension = buildExtension(track, simulcast, twcc, isKeyFrameNalu(naluType), 0);

            const auto basicPacketSize = getBasicPacketSize(mediaProtectionOverhead);
            auto packetSize = adjustPacketSize(basicPacketSize, padding, extension);

            if (packetSize >= naluDataSize) {
                // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.1
                const auto marker = parser.isAtEnd();
                const auto [rollover, sequence] = packetSource->getNextSequence();
                auto payload = ByteBuffer{ naluDataPtr, naluDataSize };
                result.push_back(
                    extension.empty()
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
            } else if (naluDataSize > 2) {
                // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
                uint8_t layerId = ((naluDataPtr[0] & 0x01) << 5) | ((naluDataPtr[1] >> 3) & 0x1F);
                uint8_t temporalId = naluDataPtr[1] & 0x07;

                auto dataPtr = naluDataPtr + 2;
                auto dataSize = naluDataSize - 2;

                auto packetNumber = 0;
                while (dataSize > 0) {
                    const auto [rollover, sequence] = packetSource->getNextSequence();

                    if (packetNumber > 0) {
                        padding = getPadding(track, simulcast, twcc, naluDataSize);
                        extension = buildExtension(track, simulcast, twcc, isKeyFrameNalu(naluType), packetNumber);
                    }

                    // The "-3" is for FU headers
                    packetSize = adjustPacketSize(basicPacketSize - 3, padding, extension);
                    if (packetNumber == 0 && packetSize >= dataSize) {
                        // The frame now fits in one packet, but a FU cannot have both start and end
                        packetSize = dataSize - 10;
                    }

                    ByteBuffer payload;
                    ByteWriter writer(payload);

                    // The payload header contains a copy of the layer id and temporal id
                    uint16_t payloadHeader = (kPacket_FU << 9) | (layerId << 3) | temporalId;
                    writer.writeU16(payloadHeader);

                    const auto isStart = packetNumber == 0;
                    const auto isEnd = dataSize <= packetSize;
                    const uint8_t fuHeader =
                        (isStart ? (1 << 7) : 0) | (isEnd ? (1 << 6) : 0) | static_cast<uint8_t>(naluType & 0x3F);
                    writer.writeU8(fuHeader);

                    const auto marker = isEnd && parser.isAtEnd();

                    const auto writeNow = std::min(dataSize, packetSize);
                    writer.write(dataPtr, writeNow);

                    result.push_back(
                        extension.empty()
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

                    dataPtr += writeNow;
                    dataSize -= writeNow;
                    packetNumber += 1;
                }
            }
        }
    }

    return result;
}

} // namespace srtc

#include "srtc/packetizer_h264.h"
#include "srtc/codec_h264.h"
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

#define LOG(level, ...) srtc::log(level, "Packetizer_H264", __VA_ARGS__)

namespace srtc
{

using namespace h264;

PacketizerH264::PacketizerH264(const std::shared_ptr<Track>& track)
    : PacketizerVideo(track)
{
    assert(track->getCodec() == Codec::H264);
}

PacketizerH264::~PacketizerH264() = default;

void PacketizerH264::setCodecSpecificData(const std::vector<ByteBuffer>& csd)
{
    mSPS.clear();
    mPPS.clear();

    for (const auto& item : csd) {
        for (NaluParser parser(item); parser; parser.next()) {
            switch (parser.currType()) {
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

    if (mSPS.empty() || mPPS.empty()) {
        LOG(SRTC_LOG_E, "Could not extract SPS and PPS from codec specific data");
    }
}

bool PacketizerH264::isKeyFrame(const ByteBuffer& frame) const
{
    for (NaluParser parser(frame); parser; parser.next()) {
        const auto naluType = parser.currType();
        if (naluType == NaluType::KeyFrame) {
            return true;
        }
    }

    return false;
}

std::list<std::shared_ptr<RtpPacket>> PacketizerH264::generate(const std::shared_ptr<RtpExtensionSource>& simulcast,
                                                               const std::shared_ptr<RtpExtensionSource>& twcc,
                                                               size_t mediaProtectionOverhead,
                                                               int64_t pts_usec,
                                                               const srtc::ByteBuffer& frame)
{
    std::list<std::shared_ptr<RtpPacket>> result;

    // https://datatracker.ietf.org/doc/html/rfc6184

    bool addedParameters = false;

    const auto track = getTrack();

    const auto timeSource = track->getRtpTimeSource();
    const auto packetSource = track->getRtpPacketSource();

    const auto frameTimestamp = timeSource->getFrameTimestamp(pts_usec);

    for (NaluParser parser(frame); parser; parser.next()) {
        const auto naluType = parser.currType();

        if (naluType == NaluType::SPS) {
            // Update SPS
            mSPS.assign(parser.currData(), parser.currDataSize());
        } else if (naluType == NaluType::PPS) {
            // Update PPS
            mPPS.assign(parser.currData(), parser.currDataSize());
        } else if (naluType == NaluType::KeyFrame) {
            // Send codec-specific data first as a STAP-A
            // https://datatracker.ietf.org/doc/html/rfc6184#section-5.7.1
            if (!addedParameters && !mSPS.empty() && !mPPS.empty()) {
                const uint8_t nri = std::max(mSPS.front() & 0x60, mPPS.front() & 0x60);

                ByteBuffer payload;
                ByteWriter writer(payload);

                // nri is already shifted left
                writer.writeU8(nri | kPacket_STAP_A);

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
            RtpExtension extension = buildExtension(track, simulcast, twcc, naluType == NaluType::KeyFrame, 0);

            const auto basicPacketSize = getBasicPacketSize(mediaProtectionOverhead);
            auto packetSize = adjustPacketSize(basicPacketSize, padding, extension);

            if (packetSize >= naluDataSize) {
                // https://datatracker.ietf.org/doc/html/rfc6184#section-5.6
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
            } else if (naluDataSize > 1) {
                // https://datatracker.ietf.org/doc/html/rfc6184#section-5.8
                const auto nri = static_cast<uint8_t>(naluDataPtr[0] & 0x60);

                // The "+1" is to skip the NALU type
                auto dataPtr = naluDataPtr + 1;
                auto dataSize = naluDataSize - 1;

                auto packetNumber = 0;
                while (dataSize > 0) {
                    const auto [rollover, sequence] = packetSource->getNextSequence();

                    if (packetNumber > 0) {
                        padding = getPadding(track, simulcast, twcc, naluDataSize);
                        extension =
                            buildExtension(track, simulcast, twcc, naluType == NaluType::KeyFrame, packetNumber);
                    }

                    // The "-2" is for FU_A headers
                    packetSize = adjustPacketSize(basicPacketSize - 2, padding, extension);
                    if (packetNumber == 0 && packetSize >= dataSize) {
                        // The frame now fits in one packet, but a FU-A cannot have both start and end
                        packetSize = dataSize - 10;
                    }

                    ByteBuffer payload;
                    ByteWriter writer(payload);

                    // nri is already shifted left
                    const uint8_t fuIndicator = nri | kPacket_FU_A;
                    writer.writeU8(fuIndicator);

                    const auto isStart = packetNumber == 0;
                    const auto isEnd = dataSize <= packetSize;
                    const uint8_t fuHeader =
                        (isStart ? (1 << 7) : 0) | (isEnd ? (1 << 6) : 0) | static_cast<uint8_t>(naluType);
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

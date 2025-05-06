#include "srtc/packetizer_h264.h"
#include "srtc/h264.h"
#include "srtc/logging.h"
#include "srtc/rtp_extension.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_extension_source.h"
#include "srtc/rtp_packet.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/rtp_time_source.h"
#include "srtc/track.h"

#include <list>

#define LOG(level, ...) srtc::log(level, "H264_pktzr", __VA_ARGS__)

namespace
{

// https://datatracker.ietf.org/doc/html/rfc6184#section-5.4

constexpr uint8_t STAP_A = 24;
constexpr uint8_t FU_A = 28;
constexpr size_t kMinPayloadSize = 600;

srtc::RtpExtension buildExtension(const std::shared_ptr<srtc::RtpExtensionSource>& simulcast,
                                  const std::shared_ptr<srtc::RtpExtensionSource>& twcc,
                                  const std::shared_ptr<srtc::Track>& track,
                                  bool isKeyFrame,
                                  int packetNumber)
{
    srtc::RtpExtension extension;

    const auto wantsSimulcast = simulcast && simulcast->wants(track, isKeyFrame, packetNumber);
    const auto wantsTWCC = twcc && twcc->wants(track, isKeyFrame, packetNumber);

    if (wantsSimulcast || wantsTWCC) {
        srtc::RtpExtensionBuilder builder;

        if (wantsSimulcast) {
            simulcast->add(builder, track, isKeyFrame, packetNumber);
        }
        if (wantsTWCC) {
            twcc->add(builder, track, isKeyFrame, packetNumber);
        }

        extension = builder.build();
    }

    return extension;
}

size_t adjustPacketSize(size_t basicPacketSize, const srtc::RtpExtension& extension)
{
    const auto extensionSize = extension.size();
    if (extensionSize == 0) {
        return basicPacketSize;
    }

    // We need to be careful with unsigned math
    if (extensionSize + kMinPayloadSize > basicPacketSize) {
        return basicPacketSize;
    }

    return basicPacketSize - extensionSize;
}

} // namespace

namespace srtc
{

using namespace h264;

PacketizerH264::PacketizerH264(const std::shared_ptr<Track>& track)
    : Packetizer(track)
{
}

PacketizerH264::~PacketizerH264() = default;

void PacketizerH264::setCodecSpecificData(const std::vector<ByteBuffer>& csd)
{
    mCSD.clear();
    for (const auto& item : csd) {
        for (NaluParser parser(item); parser; parser.next()) {
            mCSD.emplace_back(parser.currData(), parser.currDataSize());
        }
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
                                                               const srtc::ByteBuffer& frame)
{
    std::list<std::shared_ptr<RtpPacket>> result;
    uint16_t keyFrameSliceCount = 0;

    // https://datatracker.ietf.org/doc/html/rfc6184

    const auto track = getTrack();

    const auto timeSource = track->getRtpTimeSource();
    const auto packetSource = track->getRtpPacketSource();

    const auto frameTimestamp = timeSource->getCurrTimestamp();

    for (NaluParser parser(frame); parser; parser.next()) {
        const auto naluType = parser.currType();

        if (naluType == NaluType::SPS || naluType == NaluType::PPS) {
            // Update codec specific data
            if (naluType == NaluType::SPS) {
                mCSD.clear();
            }

            if (parser.currDataSize() > 0) {
                mCSD.emplace_back(parser.currData(), parser.currDataSize());
            }
        } else if (naluType == NaluType::KeyFrame) {
            // Send codec specific data first as a STAP-A
            // https://datatracker.ietf.org/doc/html/rfc6184#section-5.7.1
            if (!mCSD.empty() && keyFrameSliceCount == 0) {
                uint8_t nri = 0;
                for (const auto& csd : mCSD) {
                    nri = std::max(nri, static_cast<uint8_t>(csd.data()[0] & 0x60));
                }

                ByteBuffer payload;
                ByteWriter writer(payload);

                // nri is already shifted left
                writer.writeU8(nri | STAP_A);

                for (const auto& csd : mCSD) {
                    writer.writeU16(static_cast<uint16_t>(csd.size()));
                    writer.write(csd.data(), csd.size());
                }

                RtpExtension extension = buildExtension(simulcast, twcc, track, true, 0);

                const auto [rollover, sequence] = packetSource->getNextSequence();
                result.push_back(std::make_shared<RtpPacket>(
                    track, false, rollover, sequence, frameTimestamp, std::move(extension), std::move(payload)));
            }

            // Track slices
            keyFrameSliceCount += 1;
        }

        if (naluType == NaluType::KeyFrame || naluType == NaluType::NonKeyFrame) {
            // Now the frame itself
            const auto naluDataPtr = parser.currData();
            const auto naluDataSize = parser.currDataSize();

            RtpExtension extension = buildExtension(simulcast, twcc, track, true, 0);

            auto basicPacketSize = RtpPacket::kMaxPayloadSize - mediaProtectionOverhead - 12 /* RTP headers */;
            auto packetSize = adjustPacketSize(basicPacketSize, extension);

            if (packetSize >= naluDataSize) {
                // https://datatracker.ietf.org/doc/html/rfc6184#section-5.6
                const auto [rollover, sequence] = packetSource->getNextSequence();
                auto payload = ByteBuffer{ naluDataPtr, naluDataSize };
                result.push_back(extension.empty()
                                     ? std::make_shared<RtpPacket>(
                                           track, true, rollover, sequence, frameTimestamp, std::move(payload))
                                     : std::make_shared<RtpPacket>(track,
                                                                   true,
                                                                   rollover,
                                                                   sequence,
                                                                   frameTimestamp,
                                                                   std::move(extension),
                                                                   std::move(payload)));
            } else {
                // https://datatracker.ietf.org/doc/html/rfc6184#section-5.8
                const auto nri = static_cast<uint8_t>(naluDataPtr[0] & 0x60);

                // The "+1" is to skip the NALU type
                auto dataPtr = naluDataPtr + 1;
                auto dataSize = naluDataSize - 1;

                auto packetNumber = 0;
                while (dataSize > 0) {
                    const auto [rollover, sequence] = packetSource->getNextSequence();

                    if (packetNumber > 0) {
                        extension =
                            buildExtension(simulcast, twcc, track, naluType == NaluType::KeyFrame, packetNumber);
                    }

                    packetSize = adjustPacketSize(basicPacketSize, extension) - 2 /*  FU_A headers */;
                    if (packetNumber == 0 && packetSize >= dataSize) {
                        // The frame now fits in one packet, but a FU-A cannot
                        // have both start and end
                        packetSize = dataSize - 10;
                    }

                    ByteBuffer payload;
                    ByteWriter writer(payload);

                    // nri is already shifted left
                    const uint8_t fuIndicator = nri | FU_A;
                    writer.writeU8(fuIndicator);

                    const auto isStart = packetNumber == 0;
                    const auto isEnd = dataSize <= packetSize;
                    const uint8_t fuHeader =
                        (isStart ? (1 << 7) : 0) | (isEnd ? (1 << 6) : 0) | static_cast<uint8_t>(naluType);
                    writer.writeU8(fuHeader);

                    const auto writeNow = std::min(dataSize, packetSize);
                    writer.write(dataPtr, writeNow);

                    result.push_back(extension.empty()
                                         ? std::make_shared<RtpPacket>(
                                               track, isEnd, rollover, sequence, frameTimestamp, std::move(payload))
                                         : std::make_shared<RtpPacket>(track,
                                                                       isEnd,
                                                                       rollover,
                                                                       sequence,
                                                                       frameTimestamp,
                                                                       std::move(extension),
                                                                       std::move(payload)));

                    dataPtr += writeNow;
                    dataSize -= writeNow;
                    packetNumber += 1;
                }
            }
        }
    }

    if (keyFrameSliceCount > 1) {
        LOG(SRTC_LOG_V, "Key frame slice count: %u", keyFrameSliceCount);
    }

    return result;
}

} // namespace srtc

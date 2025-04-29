#include "srtc/packetizer_h264.h"
#include "srtc/h264.h"
#include "srtc/logging.h"
#include "srtc/rtp_packet.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/rtp_extension.h"
#include "srtc/rtp_time_source.h"
#include "srtc/track.h"

#include <list>

#define LOG(...) srtc::log("H264_pktzr", __VA_ARGS__)

namespace {

// https://datatracker.ietf.org/doc/html/rfc6184#section-5.4

constexpr uint8_t STAP_A = 24;
constexpr uint8_t FU_A = 28;
constexpr size_t kMinPayloadSize = 120;

size_t adjustPacketSize(size_t basicPacketSize, bool addExtensionToThisPacket, const srtc::RtpExtension& extension)
{
    if (!addExtensionToThisPacket) {
        return basicPacketSize;
    }

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

}

namespace srtc {

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

std::list<std::shared_ptr<RtpPacket>> PacketizerH264::generate(const RtpExtension& extension,
                                                               bool addExtensionToAllPackets,
                                                               size_t mediaProtectionOverhead,
                                                               const srtc::ByteBuffer& frame)
{
    std::list<std::shared_ptr<RtpPacket>> result;

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
            if (!mCSD.empty()) {
                uint8_t nri = 0;
                for (const auto& csd: mCSD) {
                    nri = std::max(nri, static_cast<uint8_t>(csd.data()[0] & 0x60));
                }

                ByteBuffer payload;
                ByteWriter writer(payload);

                // nri is already shifted left
                writer.writeU8(nri | STAP_A);

                for (const auto& csd: mCSD) {
                    writer.writeU16(static_cast<uint16_t>(csd.size()));
                    writer.write(csd.data(), csd.size());
                }

                const auto [rollover, sequence] = packetSource->getNextSequence();
                result.push_back(
                        std::make_shared<RtpPacket>(
                            track, false, rollover, sequence, frameTimestamp,
                            extension.copy(),
                            std::move(payload)));
            }
        }

        if (naluType == NaluType::KeyFrame || naluType == NaluType::NonKeyFrame) {
            // Now the frame itself
            const auto naluDataPtr = parser.currData();
            const auto naluDataSize = parser.currDataSize();

            auto basicPacketSize = RtpPacket::kMaxPayloadSize - mediaProtectionOverhead - 12 /* RTP headers */;

            auto addExtensionToThisPacket = addExtensionToAllPackets || naluType == NaluType::KeyFrame;
            auto packetSize = adjustPacketSize(basicPacketSize, addExtensionToThisPacket, extension);

            if (packetSize >= naluDataSize) {
                // https://datatracker.ietf.org/doc/html/rfc6184#section-5.6
                const auto [rollover, sequence] = packetSource->getNextSequence();
                auto payload = ByteBuffer { naluDataPtr, naluDataSize };
                result.push_back(
                        addExtensionToThisPacket
                        ? std::make_shared<RtpPacket>(
                            track, true, rollover, sequence, frameTimestamp,
                            extension.copy(),
                            std::move(payload))
                        : std::make_shared<RtpPacket>(
                                track, true, rollover, sequence, frameTimestamp,
                                std::move(payload))
                                );
            } else {
                // https://datatracker.ietf.org/doc/html/rfc6184#section-5.8
                const auto nri = static_cast<uint8_t>(naluDataPtr[0] & 0x60);

                // The "+1" is to skip the NALU type
                auto dataPtr = naluDataPtr + 1;
                auto dataSize = naluDataSize - 1;

                auto packetNumber = 0;
                while (dataSize > 0) {
                    const auto [rollover, sequence] = packetSource->getNextSequence();

                    addExtensionToThisPacket = addExtensionToAllPackets || naluType == NaluType::KeyFrame;
                    packetSize = adjustPacketSize(basicPacketSize, addExtensionToThisPacket, extension);
                    if (packetNumber == 0 && packetSize >= dataSize) {
                        // The frame now fits in one packet, but a FU-A cannot have both start and end
                        packetSize = dataSize - 10;
                    }
                    packetSize -= 2;    // FU_A header

                    ByteBuffer payload;
                    ByteWriter writer(payload);

                    // nri is already shifted left
                    const uint8_t fuIndicator = nri | FU_A;
                    writer.writeU8(fuIndicator);

                    const auto isStart = packetNumber == 0;
                    const auto isEnd = dataSize <= packetSize;
                    const uint8_t fuHeader =
                                    (isStart ? (1 << 7) : 0) |
                                    (isEnd ? (1 << 6) : 0) |
                                    static_cast<uint8_t>(naluType);
                    writer.writeU8(fuHeader);

                    const auto writeNow = std::min(dataSize, packetSize);
                    writer.write(dataPtr, writeNow);

                    result.push_back(
                            addExtensionToThisPacket
                            ? std::make_shared<RtpPacket>(
                                    track, isEnd, rollover, sequence,
                                    frameTimestamp,
                                    extension.copy(),
                                    std::move(payload))
                            : std::make_shared<RtpPacket>(
                                    track, isEnd, rollover, sequence,
                                    frameTimestamp,
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

}

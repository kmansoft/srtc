#include "srtc/packetizer_h264.h"
#include "srtc/h264.h"
#include "srtc/logging.h"
#include "srtc/rtp_packet.h"

#include <list>

#define LOG(...) srtc::log("H264_pktzr", __VA_ARGS__)

namespace {

// https://datatracker.ietf.org/doc/html/rfc6184#section-5.4

constexpr uint8_t STAP_A = 24;
constexpr uint8_t FU_A = 28;

}

namespace srtc {

using namespace h264;

PacketizerH264::PacketizerH264() = default;

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

std::list<RtpPacket> PacketizerH264::generate(uint8_t payloadType,
                                              uint32_t ssrc,
                                              const srtc::ByteBuffer& frame)
{
    std::list<RtpPacket> result;

    // https://datatracker.ietf.org/doc/html/rfc6184

    const auto frameTimestamp = getNextTimestamp();

    for (NaluParser parser(frame); parser; parser.next()) {
        const auto naulRefIdc = parser.currRefIdc();
        const auto naluType = parser.currType();

        if (naluType == NaluType::SPS || naluType == NaluType::PPS) {
            // Update codec specific data
            if (parser.isAtStart()) {
                mCSD.clear();
            }

            if (parser.currDataSize() > 0) {
                mCSD.emplace_back(parser.currData(), parser.currDataSize());
            }

            if (naluType == NaluType::SPS && parser.currDataSize() >= 2) {
                const uint8_t profileId = parser.currData()[1];
                LOG("Profile ID = %u", profileId);
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

                writer.writeU8(nri | STAP_A);

                for (const auto& csd: mCSD) {
                    writer.writeU16(static_cast<uint16_t>(csd.size()));
                    writer.write(csd.data(), csd.size());
                }

                result.emplace_back(false, payloadType,
                                    getNextSequence(), frameTimestamp, ssrc, payload);
            }
        }

        if (naluType == NaluType::KeyFrame || naluType == NaluType::NonKeyFrame) {
            // Now the frame itself
            const auto naluDataPtr = parser.currData();
            const auto naluDataSize = parser.currDataSize();

            auto packetSize = RtpPacket::kMaxPayloadSize;

            if (packetSize >= naluDataSize) {
                // https://datatracker.ietf.org/doc/html/rfc6184#section-5.6
                ByteBuffer payload = { parser.currData(), parser.currDataSize() };
                result.emplace_back(true, payloadType,
                                    getNextSequence(), frameTimestamp,
                                    ssrc, payload);
            } else {
                // https://datatracker.ietf.org/doc/html/rfc6184#section-5.8
                const auto nri = static_cast<uint8_t>(naluDataPtr[0] & 0x60);

                // The "+1" is to skip the NALU type
                auto dataPtr = naluDataPtr + 1;
                auto dataSize = naluDataSize - 1;

                // The frame now fits in one packet, but a FU-A cannot have both start and end
                if (packetSize == dataSize) {
                    packetSize -= 10;
                }

                auto packetNumber = 0;
                while (dataSize > 0) {
                    ByteBuffer payload;
                    ByteWriter writer(payload);

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

                    result.emplace_back(isEnd, payloadType,
                                        getNextSequence(), frameTimestamp,
                                        ssrc, payload);

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

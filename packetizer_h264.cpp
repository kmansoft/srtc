#include "srtc/packetizer_h264.h"
#include "srtc/h264.h"
#include "srtc/logging.h"
#include "srtc/rtp_packet.h"

#include <list>

#define LOG(...) srtc::log("H264_pktzr", __VA_ARGS__)

namespace {

// https://datatracker.ietf.org/doc/html/rfc6184#section-5.4

static constexpr uint8_t STAP_A = 24;

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

    for (NaluParser parser(frame); parser; parser.next()) {
        const auto naulRefIdc = parser.currRefIdc();
        const auto naluType = parser.currType();
        LOG("NALU ref = %d, type = %d, size = %zd", naulRefIdc, naluType, parser.currNaluSize());

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
                for (const auto& csd : mCSD) {
                    nri = std::max(nri, static_cast<uint8_t>(csd.data()[0] & 0x60));
                }

                ByteBuffer payload;
                ByteWriter writer(payload);

                writer.writeU8(nri | STAP_A);

                for (const auto& csd : mCSD) {
                    writer.writeU16(static_cast<uint16_t>(csd.size()));
                    writer.write(csd.data(), csd.size());
                }

                result.emplace_back(false, payloadType, getSequence(), getTimestamp(), ssrc, payload);
            }
        }
    }

    return result;
}

}

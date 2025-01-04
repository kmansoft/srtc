#include "srtc/packetizer_h264.h"
#include "srtc/h264.h"
#include "srtc/logging.h"

#define LOG(...) srtc::log("H264_pktzr", __VA_ARGS__)

namespace srtc {

using namespace h264;

PacketizerH264::PacketizerH264() = default;

PacketizerH264::~PacketizerH264() = default;

void PacketizerH264::setCodecSpecificData(const std::vector<ByteBuffer>& csd)
{
    mCSD.clear();
    for (const auto& item : csd) {
        mCSD.emplace_back(item.copy());
    }
}

void PacketizerH264::process(const srtc::ByteBuffer& frame)
{
    for (NaluParser parser(frame); parser; parser.next()) {
        const auto naluType = parser.currType();
        LOG("NALU type = %d, size = %zd", naluType, parser.currNaluSize());

        if (naluType == NaluType::SPS || naluType == NaluType::PPS) {
            if (parser.isAtStart()) {
                mCSD.clear();
            }
            mCSD.emplace_back(parser.currNalu(), parser.currNaluSize());

            if (naluType == NaluType::SPS && parser.currDataSize() >= 2) {
                const uint8_t profileId = parser.currData()[1];
                LOG("Profile ID = %u", profileId);
            }
        }
    }
}

}

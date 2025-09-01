#include "srtc/depacketizer_av1.h"

#include "srtc/codec_av1.h"
#include "srtc/logging.h"
#include "srtc/util.h"

#define LOG(level, ...) srtc::log(level, "DepacketizerAV1", __VA_ARGS__)

namespace srtc
{

DepacketizerAV1::DepacketizerAV1(const std::shared_ptr<Track>& track)
    : DepacketizerVideo(track)
    , mSeenKeyFrame(false)
{
}

DepacketizerAV1::~DepacketizerAV1() = default;

void DepacketizerAV1::reset()
{
    mSeenKeyFrame = false;
}

void DepacketizerAV1::extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList)
{
    out.clear();
}

bool DepacketizerAV1::isFrameStart(const ByteBuffer& payload) const
{
    ByteReader reader(payload);

    if (reader.remaining() >= 1) {
        // https://aomediacodec.github.io/av1-rtp-spec/#aggregation-header
        // |Z|Y|WW|N|-|-|-|
        const auto packetHeader = reader.readU8();
        const auto flagZ = (packetHeader & (1 << 7)) != 0;
        if (flagZ) {
            return false;
        }

        const auto valueW = (packetHeader >> 4) & 0x03u;
        auto obuIndex = 0u;

        while (reader.remaining() >= 1) {
            // Size unless it's the last OBU and we are told there is no size
            size_t obuSize;
            if (valueW != 0 && obuIndex == valueW - 1) {
                obuSize = reader.remaining();
            } else {
                obuSize = reader.readLEB128();
            }

            if (reader.remaining() >= 1 && obuSize >= 1) {
                // https://aomediacodec.github.io/av1-spec/#obu-header-syntax
                const auto obuHeader = reader.readU8();
                const auto obuType = (obuHeader >> 3) & 0x0Fu;
                std::printf("SUB OBU type = %u\n", obuType);
                obuSize -= 1;

                switch (obuType) {
                case av1::ObuType::Frame:
                case av1::ObuType::FrameHeader:
                case av1::ObuType::SequenceHeader:
                    return true;
                default:
                    break;
                }
            }

            if (valueW != 0 && obuIndex == valueW - 1) {
                // Reached last one
                break;
            }

            // Advance
            reader.skip(obuSize);
            obuIndex += 1;
        }
    }

    return false;
}

void DepacketizerAV1::extractImpl(std::vector<ByteBuffer>& out, const JitterBufferItem* packet, ByteBuffer&& frame)
{
}

} // namespace srtc
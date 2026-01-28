#include "srtc/simulcast_layer.h"

namespace srtc
{

void buildGoogleVLA(ByteBuffer& buf, uint8_t ridId, const std::vector<std::shared_ptr<SimulcastLayer>>& list)
{
    buf.clear();

    if (list.empty() || list.size() > 4) {
        // Invalid input
        return;
    }

    ByteWriter w(buf);

    // https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/rtp-hdrext/video-layers-allocation00

    w.writeU8((ridId << 6) | ((list.size() - 1) << 4) | 0x01);
    w.writeU8(0);

    for (const auto& layer : list) {
        w.writeLEB128(layer->kilobits_per_second);
    }

    for (const auto& layer : list) {
        w.writeU16(layer->width - 1);
        w.writeU16(layer->height - 1);
        w.writeU8(layer->frames_per_second);
    }
}

} // namespace srtc
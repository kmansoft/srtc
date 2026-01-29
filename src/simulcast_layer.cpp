#include "srtc/simulcast_layer.h"

namespace srtc
{

void buildGoogleVLA(ByteBuffer& buf, uint8_t ridId, const std::vector<std::shared_ptr<SimulcastLayer>>& list)
{
    buf.clear();

    const auto listSize = list.size();
    if (listSize == 0 || listSize > 4) {
        // Invalid input
        return;
    }
    if (ridId > 3) {
        // Invalid input
        return;
    }

    ByteWriter w(buf);

    // https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/rtp-hdrext/video-layers-allocation00

    // |RID| NS| sl_bm |
    w.writeU8((ridId << 6) | ((listSize - 1) << 4) | 0x01);
    // |sl0_bm |sl1_bm |
    // |sl2_bm |sl3_bm | - none needed because sl_bm is not zero
    // |#tl|#tl|#tl|#tl| - all zeros meaning one temporal layer per spatial layer
    w.writeU8(0);

    // bitrate in kpbs per temporal layer
    for (const auto& layer : list) {
        w.writeLEB128(layer->kilobits_per_second);
    }

    // resolution and framerate per spatial layer
    for (const auto& layer : list) {
        // width-1
        // height-1
        // max framerate
        w.writeU16(layer->width - 1);
        w.writeU16(layer->height - 1);
        w.writeU8(layer->frames_per_second);
    }
}

} // namespace srtc
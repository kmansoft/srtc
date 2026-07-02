#pragma once

#include <cstdint>
#include <vector>

#include "srtc/simulcast_layer.h"
#include "srtc/srtc.h"

namespace srtc
{

struct PubCodec {
    Codec codec = Codec::H264;
    uint32_t profile_level_id = 0; // for h264
    uint32_t minptime = 10;        // for audio
    bool stereo = false;
};

struct PubMediaItem {
    std::string media_id;
    MediaType media_type;
    std::vector<PubCodec> codec_list;
    std::vector<SimulcastLayer> layer_list;
};

struct PubMediaConfig {
    std::vector<PubMediaItem> media_list;
};

} // namespace srtc
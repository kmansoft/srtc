#pragma once

#include <cstdint>
#include <vector>

#include "srtc/simulcast_layer.h"
#include "srtc/srtc.h"

namespace srtc
{

struct PubVideoCodec {
    Codec codec;
    uint32_t profile_level_id; // for h264
};

struct PubVideoConfig {
    std::vector<PubVideoCodec> codec_list;
    std::vector<SimulcastLayer> simulcast_layer_list;
};

struct PubAudioCodec {
    Codec codec;
    uint32_t minptime;
    bool stereo;
};

struct PubAudioConfig {
    std::vector<PubAudioCodec> codec_list;
};

} // namespace srtc
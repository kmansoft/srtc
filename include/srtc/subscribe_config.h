#pragma once

#include <cstdint>
#include <vector>

#include "srtc/srtc.h"

namespace srtc
{

struct SubCodec {
	Codec codec = Codec::H264;
	uint32_t profile_level_id = 0;  // for h264
    uint32_t minptime = 10;         // for audio
    bool stereo = false;
};

struct SubMediaItem {
    std::string media_id;
    MediaType media_type;
    std::vector<SubCodec> codec_list;
};

struct SubMediaConfig {
    std::vector<SubMediaItem> media_list;
};


}

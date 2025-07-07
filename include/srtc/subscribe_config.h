#pragma once

#include <cstdint>
#include <vector>

#include "srtc/srtc.h"

namespace srtc
{

struct SubVideoCodec {
	Codec codec;
	uint32_t profile_level_id; // for h264
};

struct SubVideoConfig {
	std::vector<SubVideoCodec> codec_list;
};

struct SubAudioCodec {
	Codec codec;
	uint32_t minptime;
	bool stereo;
};

struct SubAudioConfig {
	std::vector<SubAudioCodec> codec_list;
};

}

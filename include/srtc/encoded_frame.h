#pragma once

#include "srtc/byte_buffer.h"

#include <cstdint>
#include <memory>

namespace srtc
{

class Track;

struct EncodedFrame {
	std::shared_ptr<Track> track;

	uint64_t seq_ext;
	uint64_t rtp_timestamp_ext;
    bool marker;

	ByteBuffer data;
};

} // namespace srtc
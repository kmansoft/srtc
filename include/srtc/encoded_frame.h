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
    int64_t first_to_last_packet_millis;
    int64_t wait_time_millis;

	ByteBuffer data;
};

} // namespace srtc
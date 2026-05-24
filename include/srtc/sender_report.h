#pragma once

#include "srtc/srtc.h"
#include "srtc/util.h"

#include <cstdint>
#include <chrono>

namespace srtc
{

struct SenderReport {
	std::chrono::steady_clock::time_point when;
	NtpTime ntp = {};
	uint32_t rtp = 0;
	uint32_t packet_count = 0;
	uint32_t octet_count = 0;
};

}
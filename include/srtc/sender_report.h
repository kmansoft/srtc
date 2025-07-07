#pragma once

#include "srtc/srtc.h"
#include "srtc/util.h"

#include <cstdint>
#include <chrono>

namespace srtc
{

struct SenderReport {
	std::chrono::steady_clock::time_point when;
	NtpTime ntp;
	uint32_t rtp;
	uint32_t packet_count;
	uint32_t octet_count;
};

}
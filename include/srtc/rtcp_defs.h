#pragma once

#include <cstdint>

#include "srtc/util.h"

namespace srtc
{

struct RtcpSenderReport {
	NtpTime when_sent;
	uint32_t rtp_time;

	RtcpSenderReport(const NtpTime& when_sent, uint32_t rtp_time)
		: when_sent(when_sent)
		, rtp_time(rtp_time)
	{
	}
};

} // namespace srtc
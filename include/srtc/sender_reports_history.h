#pragma once

#include "srtc/srtc.h"
#include "srtc/util.h"

#include <cstdint>

#include <unordered_map>
#include <list>
#include <chrono>

namespace srtc
{

class SenderReportsHistory
{
public:
	SenderReportsHistory();
	~SenderReportsHistory();

	void save(uint32_t ssrc, const NtpTime& ntp);
	std::optional<float> calculateRtt(uint32_t ssrc, uint32_t lastSR, uint32_t delaySinceLastSR);

private:
	struct Report {
		const NtpTime ntp;
		const std::chrono::steady_clock::time_point sent;

		Report(const NtpTime& ntp, std::chrono::steady_clock::time_point sent)
			: ntp(ntp)
			, sent(sent)
		{
		}
	};

	struct TrackHistory {
		std::list<Report> reportList;
	};

	std::unordered_map<uint32_t, TrackHistory> mTrackMap;
};

} // namespace srtc
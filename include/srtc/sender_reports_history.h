#pragma once

#include "srtc/srtc.h"
#include "srtc/util.h"

#include <cstdint>

#include <unordered_map>
#include <list>

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
		const int64_t sent_micros;

		Report(const NtpTime& ntp, int64_t sent_micros)
			: ntp(ntp)
			, sent_micros(sent_micros)
		{
		}
	};

	struct TrackHistory {
		std::list<Report> reportList;
	};

	std::unordered_map<uint32_t, TrackHistory> mTrackMap;
};

} // namespace srtc
#include "srtc/sender_reports_history.h"

namespace
{

constexpr auto kMaxHistory = 16;

}

namespace srtc
{

SenderReportsHistory::SenderReportsHistory()
{
}

SenderReportsHistory::~SenderReportsHistory() = default;

void SenderReportsHistory::save(uint32_t ssrc, const NtpTime& ntp)
{
	auto& item = mTrackMap[ssrc];
	while (item.reportList.size() >= kMaxHistory) {
		item.reportList.pop_front();
	}

	item.reportList.emplace_back(ntp, getSystemTimeMicros());
}

std::optional<float> SenderReportsHistory::calculateRtt(uint32_t ssrc, uint32_t lastSR, uint32_t delaySinceLastSR)
{
	const auto trackIter = mTrackMap.find(ssrc);
	if (trackIter != mTrackMap.end()) {
		const auto& trackItem = trackIter->second;
		for (auto iter = trackItem.reportList.rbegin(); iter != trackItem.reportList.rend(); ++iter) {
			const auto& item = *iter;
			const auto middle = (item.ntp.seconds) << 16 | (item.ntp.fraction >> 16);
			if (middle == lastSR) {
				const auto delaySinceLastSRMicros = static_cast<int64_t>(delaySinceLastSR) * 1000000 / 65536;
				const auto now = getSystemTimeMicros();
				const auto received_micros = item.sent_micros + delaySinceLastSRMicros;

				if (now >= received_micros) {
					return (now - received_micros) / 1000.0f;
				}
				break;
			}
		}
	}

	return std::nullopt;
}

} // namespace srtc
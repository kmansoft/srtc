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

	item.reportList.emplace_back(ntp, std::chrono::steady_clock::now());
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
				const auto now = std::chrono::steady_clock::now();
				const auto received = item.sent + std::chrono::microseconds(delaySinceLastSRMicros);

				if (now >= received) {
					// The 2 is so we get the actual back-and-forth (roundtrip) value
					return 2 * 1 / 1000.0f *
						   static_cast<float>(
							   std::chrono::duration_cast<std::chrono::microseconds>(now - received).count());
				}
				break;
			}
		}
	}

	return std::nullopt;
}

} // namespace srtc
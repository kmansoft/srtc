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

std::optional<float> SenderReportsHistory::calculateRtt(uint32_t ssrc, uint32_t ntpMarker, uint32_t delay)
{
	const auto trackIter = mTrackMap.find(ssrc);
	if (trackIter != mTrackMap.end()) {
		auto& trackItem = trackIter->second;
		for (auto iter = trackItem.reportList.begin(); iter != trackItem.reportList.end(); ++iter) {
			const auto middle = getNtpTimeMiddleMarker(iter->ntp);
			if (middle == ntpMarker) {
				const auto delayMicros = static_cast<int64_t>(delay) * 1000000 / 65536;
				const auto now = std::chrono::steady_clock::now();
				const auto received = iter->sent + std::chrono::microseconds(delayMicros);

				std::optional<float> result;
				if (now >= received) {
					// The 2 is so we get the actual back-and-forth (roundtrip) value
					result = 2 * 1 / 1000.0f *
							 static_cast<float>(
								 std::chrono::duration_cast<std::chrono::microseconds>(now - received).count());
				}

				trackItem.reportList.erase(iter);
				return result;
			}
		}
	}

	return std::nullopt;
}

} // namespace srtc
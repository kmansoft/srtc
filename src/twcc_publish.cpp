#include "srtc/logging.h"
#include "srtc/track.h"
#include "srtc/twcc_publish.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#define LOG(level, ...) srtc::log(level, "TWCC", __VA_ARGS__)

namespace
{

constexpr auto kMaxPacketCount = 2048u;
constexpr auto kMaxPacketMask = kMaxPacketCount - 1;
constexpr auto kMaxRecentEnough = std::chrono::milliseconds(3000);

constexpr auto kActualCalculateMinPackets = 30u;
constexpr auto kActualCalculateMinMicros = 1000u * 1000u;

constexpr auto kTrendCalculateMinPackets = 15u;
constexpr auto kTrendCalculateMinMicros = 100u * 1000u;

static_assert((kMaxPacketCount & kMaxPacketMask) == 0, "kMaxPacketCount must be a power of 2");

// TODO - measure and come up with real value
constexpr auto kSlopeThreshold = 0.1;
constexpr auto kOverusingSinceMicros = 2 * 1000 * 1000u; // Two seconds
constexpr auto kOverusingCount = 10;

constexpr auto kProbeCalculateMinPackets = 30u;
constexpr auto kProbeCalculateMinMicros = 500u * 1000u;

constexpr auto kProbeMinPacketCount = 10u;
constexpr auto kProbeMinDurationMicros = 800 * 1000u;

template <class T>
std::optional<double> calculateSlope(const std::vector<T>& list)
{
	const auto total = list.size();
	if (total == 0) {
		return std::nullopt;
	}

	double sum_x = 0, sum_y = 0;
	for (const auto& item : list) {
		sum_x += item.x;
		sum_y += item.y;
	}

	const double mean_x = sum_x / static_cast<double>(total);
	const double mean_y = sum_y / static_cast<double>(total);

	double numerator = 0;
	double denominator = 0;
	for (const auto& item : list) {
		const double dx = item.x - mean_x;
		const double dy = item.y - mean_y;
		numerator += dx * dy;
		denominator += dx * dx;
	}

	if (denominator < 0.01) {
		return std::nullopt;
	}

	return numerator / denominator;
}

} // namespace

namespace srtc::twcc
{

// FeedbackHeaderHistory

FeedbackHeaderHistory::FeedbackHeaderHistory()
	: mPacketCount(0)
{
}

FeedbackHeaderHistory::~FeedbackHeaderHistory() = default;

uint32_t FeedbackHeaderHistory::getPacketCount() const
{
	return mPacketCount;
}

void FeedbackHeaderHistory::save(const std::shared_ptr<FeedbackHeader>& header)
{
	if (mLastFbPktCount >= 0xE0 && header->fb_pkt_count <= 0x20) {
		// We wrapped
		mLastFbPktCountExpanded += 1000;
	}

	mLastFbPktCount = header->fb_pkt_count;
	header->fb_pkt_count_expanded = header->fb_pkt_count + mLastFbPktCountExpanded;

	// https://github.com/pion/webrtc/issues/3122
	for (auto iter = mHistory.begin(); iter != mHistory.end();) {
		if ((*iter)->fb_pkt_count_expanded == header->fb_pkt_count_expanded) {
			iter = mHistory.erase(iter);
		} else {
			++iter;
		}
	}

	mPacketCount += header->packet_status_count;

	if (mHistory.empty() || mHistory.back()->fb_pkt_count_expanded < header->fb_pkt_count_expanded) {
		// Can append at the end
		mHistory.push_back(header);
	} else {
		// Find the right place to insert
		auto it = mHistory.begin();
		while (it != mHistory.end() && (*it)->fb_pkt_count_expanded < header->fb_pkt_count_expanded) {
			++it;
		}
		mHistory.insert(it, header);

#ifndef NDEBUG
		for (auto curr = mHistory.begin(); curr != mHistory.end(); ++curr) {
			const auto next = std::next(curr);
			if (next == mHistory.end()) {
				break;
			}
			assert((*next)->fb_pkt_count_expanded > (*curr)->fb_pkt_count_expanded);
		}
#endif
	}

	// Trim the excess headers
	while (mPacketCount > kMaxPacketCount * 5 / 4 && !mHistory.empty()) {
		mPacketCount -= mHistory.front()->packet_status_count;
		mHistory.pop_front();
	}
}

// PacketStatusHistory

PacketStatusHistory::PacketStatusHistory()
	: mMinSeq(0)
	, mMaxSeq(0)
	, mInstantPacketLossPercent(0.0f)
	, mPacketsLostPercentFilter(0.2f)
	, mBandwidthActualFilter(0.2f)
	, mInstantTrendlineEstimate(TrendlineEstimate::kNormal)
	, mSmoothedTrendlineEstimate(TrendlineEstimate::kNormal)
	, mOverusingSinceMicros(-1)
	, mOverusingCount(0)
	, mProbeBitsPerSecond(0)
{
}

PacketStatusHistory::~PacketStatusHistory() = default;

void PacketStatusHistory::saveOutgoingPacket(uint16_t seq,
											 const std::shared_ptr<Track>& track,
											 size_t paddingSize,
											 size_t payloadSize,
											 size_t generatedSize,
											 size_t encryptedSize)
{
	PacketStatus* curr;

	if (!mHistory) {
		mMinSeq = mMaxSeq = seq;
		mHistory = std::make_unique<PacketStatus[]>(kMaxPacketCount);
		std::memset(mHistory.get(), 0, sizeof(PacketStatus) * kMaxPacketCount);
		curr = mHistory.get() + (mMaxSeq & kMaxPacketMask);
	} else {
		while (true) {
			if (((mMaxSeq + 0x10000 - mMinSeq) & 0xFFFF) + 1 == kMaxPacketCount) {
				mMinSeq += 1;
			}
			mMaxSeq += 1;
			curr = mHistory.get() + (mMaxSeq & kMaxPacketMask);
			std::memset(curr, 0, sizeof(PacketStatus));
			if (mMaxSeq == seq) {
				break;
			}
		}
	}

	curr->seq = seq;
	curr->padding_size = static_cast<uint16_t>(paddingSize);
	curr->payload_size = static_cast<uint16_t>(payloadSize);
	curr->generated_size = static_cast<uint16_t>(generatedSize);
	curr->encrypted_size = static_cast<uint16_t>(encryptedSize);
	curr->sent_time_micros = getStableTimeMicros();
	curr->media_type = track->getMediaType();
}

PacketStatus* PacketStatusHistory::get(uint16_t seq) const
{
	const auto base = mHistory.get();
	if (!base) {
		return nullptr;
	}

	if (mMinSeq <= mMaxSeq) {
		if (mMinSeq <= seq && seq <= mMaxSeq) {
			return base + (seq & kMaxPacketMask);
		}
	} else {
		if (seq >= mMinSeq || seq <= mMaxSeq) {
			return base + (seq & kMaxPacketMask);
		}
	}

	return nullptr;
}

void PacketStatusHistory::update(const std::shared_ptr<FeedbackHeader>& header)
{
	// Search backwards from max until we find a packet that's been received
	const auto max = findMostRecentReceivedPacket();
	if (!max) {
		return;
	}

	const auto now = getStableTimeMicros();
	const auto base = mHistory.get();

	// Calculate which packets have not been received
	for (uint16_t seq = max->seq;;) {
		const auto index = seq & kMaxPacketMask;
		const auto ptr = base + index;

		if (ptr->reported_checked) {
			break;
		}
		ptr->reported_checked = true;

		if (ptr->reported_status == twcc::kSTATUS_NOT_RECEIVED) {
			ptr->reported_as_not_received = true;
		}

		if (seq == mMinSeq) {
			break;
		}
		seq -= 1;
	}

	// Actual bandwidth
	if (mLastMaxForBandwidthActual.isEnough(max, kActualCalculateMinPackets, kActualCalculateMinMicros)) {
		if (calculateBandwidthActual(now, max)) {
			mLastMaxForBandwidthActual.update(max);
		}
	}

	// Probe bandwidth
	if (mLastMaxForBandwidthProbe.isEnough(max, kProbeCalculateMinPackets, kProbeCalculateMinMicros)) {
		if (calcualteBandwidthProbe(now, max)) {
			mLastMaxForBandwidthProbe.update(max);
		}
	}

	// Bandwidth trend
	if (mLastMaxForBandwidthTrend.isEnough(max, kTrendCalculateMinPackets, kTrendCalculateMinMicros)) {
		if (calculateBandwidthTrend(now, max)) {
			mLastMaxForBandwidthTrend.update(max);
		}
	}
}

uint32_t PacketStatusHistory::getPacketCount() const
{
	if (!mHistory) {
		return 0;
	}
	return (mMaxSeq - mMinSeq + 1 + 0x10000) & 0xFFFF;
}

unsigned int PacketStatusHistory::getPacingSpreadMillis(size_t totalSize,
														float bandwidthScale,
														unsigned int defaultValue) const
{
	const auto now = std::chrono::steady_clock::now();
	if (mHistory && now - mBandwidthActualFilter.getWhenUpdated() <= kMaxRecentEnough) {
		const auto bitsPerSecond = mBandwidthActualFilter.value();
		if (bitsPerSecond >= 10000.0f) {
			const auto bytesPerSecond = bitsPerSecond * bandwidthScale / 8.0f;
			const auto spread = static_cast<float>(1000 * totalSize) / bytesPerSecond;

			// The clamping assumes video fps between 15 and 60
			const auto safe = std::clamp(spread, 16.0f, 66.6f) * 0.8f;
			return static_cast<unsigned int>(safe);
		}
	}

	return defaultValue;
}

void PacketStatusHistory::updatePublishConnectionStats(PublishConnectionStats& stats)
{
	if (!mHistory) {
		return;
	}

	const auto now = std::chrono::steady_clock::time_point();

	if (now - mPacketsLostPercentFilter.getWhenUpdated() <= kMaxRecentEnough) {
		stats.packets_lost_percent = mPacketsLostPercentFilter.value();
	}

	// Actual bandwidth
	if (now - mBandwidthActualFilter.getWhenUpdated() <= kMaxRecentEnough) {
		stats.bandwidth_actual_kbit_per_second = mBandwidthActualFilter.value() / 1024.0f;
	}

	// Suggested bandwidth
	stats.bandwidth_suggested_kbit_per_second = stats.bandwidth_actual_kbit_per_second;
	if (stats.packets_lost_percent >= 10.0f || mSmoothedTrendlineEstimate == TrendlineEstimate::kOveruse) {
		// High packet loss or overuse from trendline analysis
		stats.bandwidth_suggested_kbit_per_second *= 0.9f;
	} else if (mProbeBitsPerSecond / 1024.0f > stats.bandwidth_suggested_kbit_per_second) {
		// We ran a probe and got a higher value
		stats.bandwidth_suggested_kbit_per_second = mProbeBitsPerSecond / 1024.0f;
	}
}

bool PacketStatusHistory::shouldStopProbing() const
{
	return mInstantPacketLossPercent >= 10.0f || mInstantTrendlineEstimate == TrendlineEstimate::kOveruse;
}

bool PacketStatusHistory::calculateBandwidthActual(int64_t now, PacketStatus* max)
{
	const auto base = mHistory.get();
	if (base == nullptr) {
		return false;
	}

	const auto total = getPacketCount();
	if (total < kActualCalculateMinPackets) {
		return false;
	}

	// Calculate packet loss
	uint32_t lost = 0u;
	uint32_t nacked = 0u;

	for (uint16_t seq = mMinSeq;;) {
		const auto index = seq & kMaxPacketMask;
		const auto ptr = base + index;

		if (ptr->reported_as_not_received) {
			lost += 1;
		}
		if (ptr->nack_count > 0) {
			nacked += ptr->nack_count;
		}

		if (seq == mMaxSeq) {
			break;
		}
		seq += 1;
	}

	mInstantPacketLossPercent = std::clamp<float>(
		100.0f * static_cast<float>(std::max(lost, nacked)) / static_cast<float>(total), 0.0f, 100.0f);

	mPacketsLostPercentFilter.update(mInstantPacketLossPercent);

	// Calculate actual bandwidth
	mActualItemBuf.clear();

	for (uint16_t seq = max->seq;;) {
		const auto index = seq & kMaxPacketMask;
		const auto ptr = base + index;

		if (ptr->received_time_present) {
			mActualItemBuf.emplace_back(ptr->received_time_micros, ptr->payload_size);

			if (max->received_time_micros - ptr->received_time_micros >= kActualCalculateMinMicros &&
				mActualItemBuf.size() >= kActualCalculateMinPackets) {
				break;
			}
		}

		if (seq == mMinSeq) {
			break;
		}
		seq -= 1;
	}

	if (mActualItemBuf.size() < kActualCalculateMinPackets) {
		return false;
	}

	// The buffer should be close to being sorted, but maybe not quite
	std::sort(mActualItemBuf.begin(), mActualItemBuf.end(), CompareActualItem());

	// TODO We don't really need to store packets or sort them, it's enough to keep track of min/max times
	// and total size, but let's keep this code for now in case it's needed later for something else.
	assert(mActualItemBuf.front().received_time_micros >= mActualItemBuf.back().received_time_micros);

	// Calculate duration
	const auto durationMicros =
		mActualItemBuf.front().received_time_micros - mActualItemBuf.back().received_time_micros;
	if (durationMicros < kActualCalculateMinMicros) {
		return false;
	}

	// Calculate total size
	size_t totalSize = 0;
	for (const auto& item : mActualItemBuf) {
		totalSize += item.payload_size;
	}

	const auto actualBitsPerSecond =
		(static_cast<float>(totalSize) * 8.0f * 1000000.0f) / static_cast<float>(durationMicros);
	mBandwidthActualFilter.update(actualBitsPerSecond);

	return true;
}

bool PacketStatusHistory::calcualteBandwidthProbe(int64_t now, PacketStatus* max)
{
	const auto base = mHistory.get();
	if (base == nullptr) {
		return false;
	}

	const auto total = getPacketCount();
	if (total < kProbeCalculateMinPackets) {
		return false;
	}

	// Find max span of packets with probe
	PacketStatus* endPtr = nullptr;
	PacketStatus* startPtr = nullptr;
	uint32_t totalCount = 0, paddingPresentCount = 0;
	uint32_t paddingAbsentRunCount = 0;

	for (uint16_t seq = mMaxSeq;;) {
		const auto index = seq & kMaxPacketMask;
		const auto ptr = base + index;

		if (ptr->padding_size > 0) {
			if (endPtr == nullptr) {
				endPtr = ptr;
			}
			totalCount += 1;
			paddingPresentCount += 1;
			paddingAbsentRunCount = 0;
		} else if (endPtr != nullptr) {
			if (ptr->media_type != MediaType::Audio) {
				// Audio packets may not be able to add padding
				totalCount += 1;
				paddingAbsentRunCount += 1;
			}
		}

		if (endPtr != nullptr && ptr->padding_size > 0) {
			if (paddingPresentCount >= kProbeMinPacketCount && paddingPresentCount >= totalCount * 8 / 10) {
				if (endPtr->received_time_micros - ptr->received_time_micros >= kProbeMinDurationMicros) {
					startPtr = ptr;
				}
			}

			if (startPtr != nullptr) {
				if (paddingAbsentRunCount >= 10) {
					break;
				}
			}
		}

		if (seq == mMinSeq) {
			break;
		}
		seq -= 1;
	}

	if (startPtr == nullptr || endPtr == nullptr) {
		return false;
	}

	// Calculate bandwidth use and count of the span we found
	unsigned int spanTotalCount = 0, spanPaddingCount = 0;
	size_t spanDataSize = 0;
	for (uint16_t seq = endPtr->seq;;) {
		const auto index = seq & kMaxPacketMask;
		const auto ptr = base + index;

		spanTotalCount += 1;
		if (ptr->padding_size > 0) {
			spanPaddingCount += 1;
		}
		spanDataSize += ptr->payload_size + ptr->padding_size;

		if (seq == startPtr->seq) {
			break;
		}
		seq -= 1;
	}

	const auto spanDurationMicros = endPtr->received_time_micros - startPtr->received_time_micros;

	const auto probeBitsPerSecond =
		(static_cast<float>(spanDataSize) * 8.0f * 1000000.0f) / static_cast<float>(spanDurationMicros);

	LOG(SRTC_LOG_V,
		"Probing packets: min = %u, max = %u, "
		"span total = %u, span probing = %u, duration = %" PRIu64 " us, "
		"data size = %zu, bw = %.2f kbit/s",
		startPtr->seq,
		endPtr->seq,
		spanTotalCount,
		spanPaddingCount,
		spanDurationMicros,
		spanDataSize,
		probeBitsPerSecond / 1024.0f);

	mProbeBitsPerSecond = probeBitsPerSecond;

	return true;
}

bool PacketStatusHistory::calculateBandwidthTrend(int64_t now, PacketStatus* max)
{
	const auto base = mHistory.get();
	if (base == nullptr) {
		return false;
	}

	const auto total = getPacketCount();
	if (total == 0) {
		return false;
	}

	// Calculate time deltas
	mTrendItemBuf.clear();

	for (uint16_t curr_seq = max->seq;;) {
		const auto curr_ptr = base + (curr_seq & kMaxPacketMask);

		if (curr_ptr->received_time_present) {

			if (curr_seq != mMinSeq) {
				const uint16_t prev_seq = curr_seq - 1;
				const auto prev_ptr = base + (prev_seq & kMaxPacketMask);

				if (prev_ptr->received_time_present) {

					const auto sent_millis = static_cast<double>(curr_ptr->sent_time_micros) / 1000.0;

					const auto sent_delta_micros = curr_ptr->sent_time_micros - prev_ptr->sent_time_micros;
					const auto received_delta_micros = curr_ptr->received_time_micros - prev_ptr->received_time_micros;
					const auto inter_delta_millis =
						static_cast<double>(received_delta_micros - sent_delta_micros) / 1000.0;

					mTrendItemBuf.emplace_back(static_cast<double>(sent_millis), inter_delta_millis);

					if (max->received_time_micros - curr_ptr->received_time_micros >= kTrendCalculateMinMicros &&
						mTrendItemBuf.size() >= kTrendCalculateMinPackets) {
						break;
					}
				}
			}
		}

		if (curr_seq == mMinSeq) {
			break;
		}
		curr_seq -= 1;
	}

	if (mTrendItemBuf.size() < kTrendCalculateMinPackets) {
		return false;
	}

	std::reverse(mTrendItemBuf.begin(), mTrendItemBuf.end());

	const auto slope = calculateSlope(mTrendItemBuf);
	if (!slope.has_value()) {
		return false;
	}

	LOG(SRTC_LOG_V, "Slope = %.4f", slope.value());

	if (slope.value() >= kSlopeThreshold) {
		// Overuse, make sure it's not a random one time event
		if (mOverusingSinceMicros == -1) {
			mOverusingSinceMicros = now;
			mOverusingCount = 0;
		}
		mOverusingCount += 1;

		mInstantTrendlineEstimate = TrendlineEstimate::kOveruse;
		if (now - mOverusingSinceMicros >= kOverusingSinceMicros && mOverusingCount >= kOverusingCount) {
			mSmoothedTrendlineEstimate = TrendlineEstimate::kOveruse;
			mProbeBitsPerSecond = 0.0f;
		}
	} else if (slope.value() <= -kSlopeThreshold) {
		// Underuse
		mOverusingSinceMicros = -1;
		mOverusingCount = 0;

		mInstantTrendlineEstimate = TrendlineEstimate::kUnderuse;
		mSmoothedTrendlineEstimate = TrendlineEstimate::kUnderuse;
	} else {
		// Normal
		mOverusingSinceMicros = -1;
		mOverusingCount = 0;

		mInstantTrendlineEstimate = TrendlineEstimate::kNormal;
		mSmoothedTrendlineEstimate = TrendlineEstimate::kNormal;
	}

	return true;
}

PacketStatus* PacketStatusHistory::findMostRecentReceivedPacket() const
{
	PacketStatus* base = mHistory.get();
	if (!base) {
		return nullptr;
	}

	for (uint16_t seq = mMaxSeq;;) {
		const auto index = seq & kMaxPacketMask;
		const auto ptr = base + index;

		if (ptr->reported_status == twcc::kSTATUS_RECEIVED_SMALL_DELTA ||
			ptr->reported_status == twcc::kSTATUS_RECEIVED_LARGE_DELTA) {
			return ptr;
		}
		if (seq == mMinSeq) {
			break;
		}
		seq -= 1;
	}

	return nullptr;
}

} // namespace srtc::twcc

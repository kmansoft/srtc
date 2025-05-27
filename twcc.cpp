#include "srtc/twcc.h"
#include "srtc/util.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>

namespace
{

constexpr auto kMaxPacketCount = 2048;
constexpr auto kMaxPacketMask = kMaxPacketCount - 1;
constexpr auto kBandwidthTimePeriodMicros = 1200000; // 1.2 seconds
constexpr auto kBandwidthTimeEnoughMicros = 500000;	 // 0.5 seconds
constexpr auto kMaxDataRecentEnoughMicros = 3000000;

static_assert((kMaxPacketCount & kMaxPacketMask) == 0, "kMaxPacketCount must be a power of 2");

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
	, mPacketsLostFilter(0.2f)
	, mRttFilter(0.2f)
	, mBandwidthActualFilter(0.2f)
	, mBandwidthSuggestedFilter(0.2f)
	, mLastUpdated(0)
{
}

PacketStatusHistory::~PacketStatusHistory() = default;

void PacketStatusHistory::save(uint16_t seq, size_t payloadSize, size_t generatedSize, size_t encryptedSize)
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
	curr->payload_size = static_cast<uint16_t>(payloadSize);
	curr->generated_size = static_cast<uint16_t>(generatedSize);
	curr->encrypted_size = static_cast<uint16_t>(encryptedSize);
	curr->sent_time_micros = getSystemTimeMicros();

	if (mMinSeq != mMaxSeq) {
		const auto prev = mHistory.get() + ((mMaxSeq + 0x10000 - 1) & kMaxPacketMask);
		curr->sent_delta_micros = static_cast<int32_t>(curr->sent_time_micros - prev->sent_time_micros);
	}
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

	const auto now = getSystemTimeMicros();
	const auto base = mHistory.get();

	// Update the RTT
	if (((mMaxSeq - max->seq) & 0xFFFFu) <= 100) {
		const auto rtt = static_cast<float>(now - max->sent_time_micros) / 1000.f;
		mRttFilter.update(rtt, now);
	}

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

	// Update stats at most once per second
	const auto total = getPacketCount();
	if (now >= mLastUpdated + 1000000 && total >= 10) {
		mLastUpdated = now;

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

		const auto value = std::clamp<float>(
			100.0f * static_cast<float>(std::max(lost, nacked)) / static_cast<float>(total), 0.0f, 100.0f);
		mPacketsLostFilter.update(value, now);

		// Calculate base bandwidth
		mReceivedPacketBuf.clear();

		for (uint16_t seq = max->seq;;) {
			const auto index = seq & kMaxPacketMask;
			const auto ptr = base + index;

			if (ptr->reported_status == twcc::kSTATUS_RECEIVED_SMALL_DELTA ||
				ptr->reported_status == twcc::kSTATUS_RECEIVED_LARGE_DELTA) {

				mReceivedPacketBuf.emplace_back(ptr->received_time_micros, ptr->generated_size);

				if (max->received_time_micros - ptr->received_time_micros >= kBandwidthTimePeriodMicros &&
					mReceivedPacketBuf.size() >= 10) {
					break;
				}
			}

			if (seq == mMinSeq) {
				break;
			}
			seq -= 1;
		}

		if (mReceivedPacketBuf.size() >= 10) {
			// The buffer should be close to being sorted, but maybe not quite
			std::sort(mReceivedPacketBuf.begin(), mReceivedPacketBuf.end(), CompareReceivedPacket());

			// TODO We don't really need to store received packets or sort them, it's enough to keep track of min/max times
			// but let's keep this code for now in case it's needed for more advanced banwidth estimation
			const auto size = mReceivedPacketBuf.size();
			const auto temp = mReceivedPacketBuf.data();
			assert(temp[0].received_time_micros >= temp[size - 1].received_time_micros);

			// Calculate duration
			const auto durationMicros = temp[0].received_time_micros - temp[size - 1].received_time_micros;
			if (durationMicros >= kBandwidthTimeEnoughMicros) {
				// Calculate total size
				int64_t totalSize = 0;
				for (size_t i = 0; i < size; ++i) {
					totalSize += temp[i].size;
				}

				const auto actualBitsPerSecond =
					(static_cast<float>(totalSize) * 8.0f * 1000000.0f) / static_cast<float>(durationMicros);
				mBandwidthActualFilter.update(actualBitsPerSecond, now);

				auto suggestedBitsPerSecond = actualBitsPerSecond;
				if (mPacketsLostFilter.value() >= 0.1f) {
					// Experi
					suggestedBitsPerSecond *= 0.9f;
				}
				mBandwidthSuggestedFilter.update(suggestedBitsPerSecond, now);
			}
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
	const auto now = getSystemTimeMicros();
	if (mHistory && now - mBandwidthActualFilter.getTimestamp() <= kMaxDataRecentEnoughMicros) {
		const auto bitsPerSecond = mBandwidthActualFilter.value();
		if (bitsPerSecond >= 100000.0f) {
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
	const auto now = getSystemTimeMicros();
	if (mHistory && now - mLastUpdated <= kMaxDataRecentEnoughMicros) {
		stats.packets_lost_percent = mPacketsLostFilter.value();
		stats.rtt_ms = mRttFilter.value();
	} else {
		stats.packets_lost_percent = 0.0f;
		stats.rtt_ms = 0.0f;
	}

	// Actual bandwidth
	if (mHistory && now - mBandwidthActualFilter.getTimestamp() <= kMaxDataRecentEnoughMicros) {
		stats.bandwidth_actual_kbit_per_second = mBandwidthActualFilter.value() / 1024.0f;
	} else {
		stats.bandwidth_actual_kbit_per_second = 0.0f;
	}

	// Suggested bandwidth
	if (mHistory && now - mBandwidthSuggestedFilter.getTimestamp() <= kMaxDataRecentEnoughMicros) {
		stats.bandwidth_suggested_kbit_per_second = mBandwidthSuggestedFilter.value() / 1024.0f;
	}
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

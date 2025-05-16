#include "srtc/twcc.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>

namespace
{

constexpr auto kMaxPacketCount = 1024;
constexpr auto kMaxPacketMask = kMaxPacketCount - 1;

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
	header->fb_pkt_expanded = header->fb_pkt_count + mLastFbPktCountExpanded;

	// https://github.com/pion/webrtc/issues/3122
	for (auto iter = mHistory.begin(); iter != mHistory.end();) {
		if ((*iter)->fb_pkt_expanded == header->fb_pkt_expanded) {
			std::printf("RTCP TWCC packet: removing duplicate fb_pkt_expanded=%u\n", header->fb_pkt_expanded);
			iter = mHistory.erase(iter);
		} else {
			++iter;
		}
	}

	mPacketCount += header->packet_status_count;

	if (mHistory.empty() || mHistory.back()->fb_pkt_expanded < header->fb_pkt_expanded) {
		// Can append at the end
		mHistory.push_back(header);
	} else {
		// Find the right place to insert
		auto it = mHistory.begin();
		while (it != mHistory.end() && (*it)->fb_pkt_expanded < header->fb_pkt_expanded) {
			++it;
		}
		mHistory.insert(it, header);

#ifndef NDEBUG
		for (auto curr = mHistory.begin(); curr != mHistory.end(); ++curr) {
			const auto next = std::next(curr);
			if (next == mHistory.end()) {
				break;
			}
			assert((*next)->fb_pkt_expanded > (*curr)->fb_pkt_expanded);
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
{
}

PacketStatusHistory::~PacketStatusHistory() = default;

void PacketStatusHistory::save(uint16_t seq, size_t payloadSize, size_t encryptedSize)
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

	struct timespec now = {};
	clock_gettime(CLOCK_MONOTONIC, &now);
	curr->seq = seq;
	curr->payload_size = payloadSize;
	curr->encrypted_size = encryptedSize;
	curr->sent_time_micros = now.tv_sec * 1000000 + now.tv_nsec / 1000;

	if (mMinSeq != mMaxSeq) {
		const auto prev = mHistory.get() + ((mMaxSeq + 0x10000 - 1) & kMaxPacketMask);
		curr->sent_delta_micros = static_cast<int32_t>(curr->sent_time_micros - prev->sent_time_micros);
	}
}

// may return nullptr
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

uint32_t PacketStatusHistory::getPacketCount() const
{
	if (!mHistory) {
		return 0;
	}
	return (mMaxSeq - mMinSeq + 1 + 0x10000) & 0xFFFF;
}

float PacketStatusHistory::getPacketsLostPercent() const
{
	const auto total = getPacketCount();
	if (total == 0) {
		return 0.0f;
	}

	uint32_t lost = 0u;
	uint32_t with_time = 0u;

	const auto base = mHistory.get();
	for (uint16_t seq = mMinSeq;;) {
		const auto index = seq & kMaxPacketMask;
		const auto ptr = base + index;

		if (ptr->nack_count > 0) {
			lost += ptr->nack_count;
		}
		if (ptr->reported_time_micros > 0) {
			with_time += 1;
		}

		if (seq == mMaxSeq) {
			break;
		}
		seq += 1;
	}

	std::printf("*** RTCP TWCC packet: lost=%u with_time=%u total=%u\n", lost, with_time, total);

	return std::clamp<float>(100.0f * static_cast<float>(lost) / static_cast<float>(total), 0.0f, 100.0f);
}

} // namespace srtc::twcc
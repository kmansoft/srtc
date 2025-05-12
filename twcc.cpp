#include "srtc/twcc.h"

#include <cassert>

namespace
{

constexpr auto kMaxPacketCount = 512;

}

namespace srtc::twcc
{
FeedbackHeaderHistory::FeedbackHeaderHistory()
    : mPacketCount(0)
{
}

FeedbackHeaderHistory::~FeedbackHeaderHistory() = default;

uint32_t FeedbackHeaderHistory::getPacketCount() const
{
    return mPacketCount;
}

float FeedbackHeaderHistory::getPacketsLostPercent() const
{
    if (mPacketCount == 0) {
        return 0.0f;
    }

    uint32_t lostCount = 0;
    for (const auto& header : mHistory) {
        lostCount += header->packet_lost_count;
    }

    return static_cast<float>(lostCount) / static_cast<float>(mPacketCount);
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
    while (mPacketCount > kMaxPacketCount) {
        const auto front = mHistory.front();
        mPacketCount -= front->packet_status_count;
        mHistory.pop_front();
    }
}
} // namespace srtc::twcc
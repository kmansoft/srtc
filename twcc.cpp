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

float FeedbackHeaderHistory::getPacketLostPercent() const
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
    mPacketCount += header->packet_status_count;

    if (!mHistory.empty()) {
        const auto last = mHistory.back();

        if (last->fb_pkt_count >= 0xF0 && header->fb_pkt_count <= 0x10) {
            // We wrapped
            header->fb_pkt_count_expanded = ((last->fb_pkt_count_expanded & ~0xFFu) + 0x100) | header->fb_pkt_count;
        } else {
            // No wrapping, but carry over the expanded count
            header->fb_pkt_count_expanded = (last->fb_pkt_count_expanded & ~0xFFu) | header->fb_pkt_count;
        }
    }

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
    while (mPacketCount > kMaxPacketCount) {
        const auto front = mHistory.front();
        mPacketCount -= front->packet_status_count;
        mHistory.pop_front();
    }
}
} // namespace srtc::twcc
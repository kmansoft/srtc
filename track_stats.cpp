#include "srtc/track_stats.h"

namespace srtc
{

TrackStats::TrackStats()
    : mSentPackets(0)
    , mSentBytes(0)
{
}

TrackStats::~TrackStats() = default;

void TrackStats::clear()
{
    mSentPackets = 0;
    mSentBytes = 0;
}

uint32_t TrackStats::getSentPackets() const
{
    return mSentPackets;
}

uint32_t TrackStats::getSentBytes() const
{
    return mSentBytes;
}

void TrackStats::incrementSentPackets(uint32_t increment)
{
    mSentPackets += increment;
}

void TrackStats::incrementSentBytes(uint32_t increment)
{
    mSentBytes += increment;
}

} // namespace srtc
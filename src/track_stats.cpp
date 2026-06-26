#include "srtc/track_stats.h"

namespace srtc
{

TrackStats::TrackStats()
    : mSentFrames(0)
    , mSentPackets(0)
    , mSentBytes(0)
    , mReceivedFrames(0)
    , mReceivedPackets(0)
    , mReceivedBytes(0)
    , mReceivedPacketsLost(0)
{
}

TrackStats::~TrackStats() = default;

void TrackStats::clear()
{
    mSentFrames = 0;
    mSentPackets = 0;
    mSentBytes = 0;
    mReceivedFrames = 0;
    mReceivedPackets = 0;
    mReceivedBytes = 0;
    mReceivedPacketsLost = 0;
}

size_t TrackStats::getSentFrames() const
{
    return mSentFrames;
}

size_t TrackStats::getSentPackets() const
{
    return mSentPackets;
}

size_t TrackStats::getSentBytes() const
{
    return mSentBytes;
}

void TrackStats::incrementSentFrames(size_t increment)
{
    mSentFrames += increment;
}

void TrackStats::incrementSentPackets(size_t increment)
{
    mSentPackets += increment;
}

void TrackStats::incrementSentBytes(size_t increment)
{
    mSentBytes += increment;
}

size_t TrackStats::getReceivedFrames() const
{
    return mReceivedFrames;
}

size_t TrackStats::getReceivedPackets() const
{
    return mReceivedPackets;
}

size_t TrackStats::getReceivedBytes() const
{
    return mReceivedBytes;
}

size_t TrackStats::getReceivedPacketsLost() const
{
    return mReceivedPacketsLost;
}

void TrackStats::setHighestReceivedSeq(uint16_t seq)
{
    (void)mReceivedHighestSeq.extend(seq);
}

uint64_t TrackStats::getReceivedHighestSeqEx() const
{
    const auto value = mReceivedHighestSeq.get();
    if (value.has_value()) {
        return value.value();
    }
    return 0;
}

void TrackStats::incrementReceivedFrames(size_t increment)
{
    mReceivedFrames += increment;
}


void TrackStats::incrementReceivedPackets(size_t increment)
{
    mReceivedPackets += increment;
}

void TrackStats::incrementReceivedBytes(size_t increment)
{
    mReceivedBytes += increment;
}

void TrackStats::incrementReceivedPacketsLost(size_t increment)
{
    mReceivedPacketsLost += increment;
}

void TrackStats::setReceivedSenderReport(const SenderReport& sr)
{
    mReceivedSenderReport = sr;
}

std::optional<SenderReport> TrackStats::getReceivedSenderReport() const
{
    return mReceivedSenderReport;
}

} // namespace srtc

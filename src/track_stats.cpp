#include "srtc/track_stats.h"

namespace srtc
{

TrackStats::TrackStats()
	: mSentPackets(0)
	, mSentBytes(0)
	, mReceivedPackets(0)
	, mReceivedBytes(0)
{
}

TrackStats::~TrackStats() = default;

void TrackStats::clear()
{
	mSentPackets = 0;
	mSentBytes = 0;
}

size_t TrackStats::getSentPackets() const
{
	return mSentPackets;
}

size_t TrackStats::getSentBytes() const
{
	return mSentBytes;
}

void TrackStats::incrementSentPackets(size_t increment)
{
	mSentPackets += increment;
}

void TrackStats::incrementSentBytes(size_t increment)
{
	mSentBytes += increment;
}

size_t TrackStats::getReceivedPackets() const
{
	return mReceivedPackets;
}

size_t TrackStats::getReceivedBytes() const
{
	return mReceivedBytes;
}

void TrackStats::setHighestReceivedSeq(uint16_t seq)
{
	(void) mReceivedHighestSeq.extend(seq);
}

uint64_t TrackStats::getReceivedHighestSeqEx() const
{
	const auto value = mReceivedHighestSeq.get();
	if (value.has_value()) {
		return value.value();
	}
	return 0;
}


void TrackStats::incrementReceivedPackets(size_t increment)
{
	mReceivedPackets += increment;
}

void TrackStats::incrementReceivedBytes(size_t increment)
{
	mReceivedBytes += increment;
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

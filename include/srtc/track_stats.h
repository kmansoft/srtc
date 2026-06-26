#pragma once

#include <atomic>
#include <optional>

#include "srtc/sender_report.h"
#include "srtc/extended_value.h"

namespace srtc
{

class TrackStats
{
public:
    TrackStats();
    ~TrackStats();

    void clear();

    [[nodiscard]] size_t getSentFrames() const;
    [[nodiscard]] size_t getSentPackets() const;
    [[nodiscard]] size_t getSentBytes() const;

    void incrementSentFrames(size_t increment);
    void incrementSentPackets(size_t increment);
    void incrementSentBytes(size_t increment);

    [[nodiscard]] size_t getReceivedFrames() const;
	[[nodiscard]] size_t getReceivedPackets() const;
	[[nodiscard]] size_t getReceivedBytes() const;
    [[nodiscard]] size_t getReceivedPacketsLost() const;

	void setHighestReceivedSeq(uint16_t seq);
	[[nodiscard]] uint64_t getReceivedHighestSeqEx() const;

    void incrementReceivedFrames(size_t increment);
	void incrementReceivedPackets(size_t increment);
	void incrementReceivedBytes(size_t increment);
    void incrementReceivedPacketsLost(size_t increment);

	void setReceivedSenderReport(const SenderReport& sr);
	[[nodiscard]] std::optional<SenderReport> getReceivedSenderReport() const;

private:
    size_t mSentFrames;
    size_t mSentPackets;
    size_t mSentBytes;
    size_t mReceivedFrames;
    size_t mReceivedPackets;
    size_t mReceivedBytes;
    size_t mReceivedPacketsLost;
	ExtendedValue<uint16_t> mReceivedHighestSeq;
	std::optional<SenderReport> mReceivedSenderReport;
};

} // namespace srtc
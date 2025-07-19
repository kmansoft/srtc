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

    [[nodiscard]] size_t getSentPackets() const;
    [[nodiscard]] size_t getSentBytes() const;

    void incrementSentPackets(size_t increment);
    void incrementSentBytes(size_t increment);

	[[nodiscard]] size_t getReceivedPackets() const;
	[[nodiscard]] size_t getReceivedBytes() const;

	void setHighestReceivedSeq(uint16_t seq);
	[[nodiscard]] uint64_t getReceivedHighestSeqEx() const;

	void incrementReceivedPackets(size_t increment);
	void incrementReceivedBytes(size_t increment);

	void setReceivedSenderReport(const SenderReport& sr);
	[[nodiscard]] std::optional<SenderReport> getReceivedSenderReport() const;

private:
    size_t mSentPackets;
    size_t mSentBytes;
    size_t mReceivedPackets;
    size_t mReceivedBytes;
	ExtendedValue<uint16_t> mReceivedHighestSeq;
	std::optional<SenderReport> mReceivedSenderReport;
};

} // namespace srtc
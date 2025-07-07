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

    [[nodiscard]] uint32_t getSentPackets() const;
    [[nodiscard]] uint32_t getSentBytes() const;

    void incrementSentPackets(uint32_t increment);
    void incrementSentBytes(uint32_t increment);

	[[nodiscard]] uint32_t getReceivedPackets() const;
	[[nodiscard]] uint32_t getReceivedBytes() const;

	void setHighestReceivedSeq(uint16_t seq);
	[[nodiscard]] uint64_t getReceivedHighestSeqEx() const;

	void incrementReceivedPackets(uint32_t increment);
	void incrementReceivedBytes(uint32_t increment);

	void setReceivedSenderReport(const SenderReport& sr);
	[[nodiscard]] std::optional<SenderReport> getReceivedSenderReport() const;

private:
    uint32_t mSentPackets;
    uint32_t mSentBytes;
	uint32_t mReceivedPackets;
	uint32_t mReceivedBytes;
	ExtendedValue<uint16_t> mReceivedHighestSeq;
	std::optional<SenderReport> mReceivedSenderReport;
};

} // namespace srtc
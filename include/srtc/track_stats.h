#pragma once

#include <atomic>
#include <optional>

#include "srtc/rtcp_defs.h"

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

	void setSenderReport(const RtcpSenderReport& senderReport);

private:
    uint32_t mSentPackets;
    uint32_t mSentBytes;

	std::optional<RtcpSenderReport> mSenderReport;
};

} // namespace srtc
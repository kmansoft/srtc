#pragma once

#include <cstdint>
#include <list>
#include <memory>

// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-
namespace srtc::twcc
{
// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.3
constexpr auto kCHUNK_RUN_LENGTH = 0;
// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
constexpr auto kCHUNK_STATUS_VECTOR = 1;

// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.1
constexpr auto kSTATUS_NOT_RECEIVED = 0;
constexpr auto kSTATUS_RECEIVED_SMALL_DELTA = 1;
constexpr auto kSTATUS_RECEIVED_LARGE_DELTA = 2;
constexpr auto kSTATUS_RECEIVED_NO_TS = 3;

// A single RTCP feedback packet can contain statuses and timestamps of multiple RTP packets
struct FeedbackHeader {
    const uint16_t base_seq_number;
    const uint16_t packet_status_count;
    const int32_t reference_time;
    const uint16_t fb_pkt_count;

    uint16_t fb_pkt_expanded;
    uint16_t packet_lost_count;

    FeedbackHeader(uint16_t base_seq_number, uint16_t packet_status_count, int32_t reference_time, uint8_t fb_pkt_count)
        : base_seq_number(base_seq_number)
        , packet_status_count(packet_status_count)
        , reference_time(reference_time)
        , fb_pkt_count(fb_pkt_count)
        , fb_pkt_expanded(fb_pkt_count)
        , packet_lost_count(0)
    {
    }
};

class FeedbackHeaderHistory
{
public:
    FeedbackHeaderHistory();
    ~FeedbackHeaderHistory();

    void save(const std::shared_ptr<FeedbackHeader>& header);

    [[nodiscard]] uint32_t getPacketCount() const;
    [[nodiscard]] float getPacketsLostPercent() const;

private:
    uint32_t mPacketCount;
    std::list<std::shared_ptr<FeedbackHeader>> mHistory;

    uint16_t mLastFbPktCount = 0;
    uint16_t mLastFbPktCountExpanded = 0;
};

// Status of a single RTP packet
struct PacketStatus {
    const uint16_t seq;
    const uint8_t status;
    int32_t delta_micros;

    PacketStatus(uint16_t seq, uint8_t status)
        : seq(seq)
        , status(status)
        , delta_micros(0)
    {
    }
};

} // namespace srtc::twcc

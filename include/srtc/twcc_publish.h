#pragma once

#include <cstdint>
#include <ctime>
#include <list>
#include <memory>
#include <optional>
#include <vector>

#include "srtc/srtc.h"
#include "srtc/temp_buffer.h"
#include "srtc/util.h"

namespace srtc
{
class RtpPacket;
class Track;
} // namespace srtc

namespace srtc::twcc
{
// Status of a single published RTP packet

struct PublishPacket {
    int64_t sent_time_micros;
    int64_t received_time_micros;

    uint16_t padding_size;
    uint16_t payload_size;
    uint16_t generated_size;
    uint16_t encrypted_size;

    uint16_t seq;
    uint16_t nack_count;

    MediaType media_type;
    uint8_t reported_status;

    bool reported_as_not_received;
    bool reported_checked;
    bool received_time_present;
};

// A history of such packets

class PublishPacketHistory
{
public:
    PublishPacketHistory();
    ~PublishPacketHistory();

    void saveOutgoingPacket(uint16_t seq,
                            const std::shared_ptr<Track>& track,
                            size_t paddingSize,
                            size_t payloadSize,
                            size_t generatedSize,
                            size_t encryptedSize);

    // may return nullptr
    [[nodiscard]] PublishPacket* get(uint16_t seq) const;

    void update();

    [[nodiscard]] uint32_t getPacketCount() const;

    [[nodiscard]] unsigned int getPacingSpreadMillis(size_t totalSize,
                                                     float bandwidthScale,
                                                     unsigned int defaultValue) const;
    void updatePublishConnectionStats(PublishConnectionStats& stats);

    enum class TrendlineEstimate {
        kNormal,
        kOveruse,
        kUnderuse
    };

    [[nodiscard]] bool shouldStopProbing() const;

private:
    uint16_t mMinSeq;
    uint16_t mMaxSeq; // Closed intervals: [mMinSeq, mMaxSeq]

    PublishPacket* mPacketList;
    float mInstantPacketLossPercent;
    Filter<float> mPacketsLostPercentFilter;
    Filter<float> mBandwidthActualFilter;
    TrendlineEstimate mInstantTrendlineEstimate;
    TrendlineEstimate mSmoothedTrendlineEstimate;
    int64_t mOverusingSinceMicros;
    uint16_t mOverusingCount;
    float mProbeBitsPerSecond;

    struct LastPacketInfo {
        uint16_t seq;
        int64_t sent_time_micros;

        LastPacketInfo()
            : seq(0)
            , sent_time_micros(0)
        {
        }

        [[nodiscard]] bool isEnough(const PublishPacket* max, unsigned int minPackets, unsigned int minMicros) const
        {
            return static_cast<uint16_t>(max->seq - seq) >= minPackets &&
                   max->sent_time_micros - sent_time_micros >= minMicros;
        }

        void update(const PublishPacket* max)
        {
            seq = max->seq;
            sent_time_micros = max->sent_time_micros;
        }
    };

    LastPacketInfo mLastMaxForBandwidthActual;
    LastPacketInfo mLastMaxForBandwidthProbe;
    LastPacketInfo mLastMaxForBandwidthTrend;

    struct ActualItem {
        uint64_t received_time_micros;
        uint16_t payload_size;

        ActualItem(uint64_t received_time_micros, uint16_t payload_size)
            : received_time_micros(received_time_micros)
            , payload_size(payload_size)
        {
        }
    };

    struct CompareActualItem {
        bool operator()(const ActualItem& a, const ActualItem& b) const
        {
            return a.received_time_micros > b.received_time_micros;
        }
    };

    std::vector<ActualItem> mActualItemBuf;

    struct TrendItem {
        double x;
        double y;

        TrendItem(double x, double y)
            : x(x)
            , y(y)
        {
        }
    };

    std::vector<TrendItem> mTrendItemBuf;

    bool calculateBandwidthActual(int64_t now, PublishPacket* max);
    bool calcualteBandwidthProbe(int64_t now, PublishPacket* max);
    bool calculateBandwidthTrend(int64_t now, PublishPacket* max);

    [[nodiscard]] PublishPacket* findMostRecentReceivedPacket() const;
};

} // namespace srtc::twcc

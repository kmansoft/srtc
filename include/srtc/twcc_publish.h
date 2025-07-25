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
}
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
	const uint16_t fb_pkt_count;
	const int64_t reference_time_micros;

	uint16_t fb_pkt_count_expanded;

	FeedbackHeader(uint16_t base_seq_number, uint16_t packet_status_count, int32_t reference_time, uint8_t fb_pkt_count)
		: base_seq_number(base_seq_number)
		, packet_status_count(packet_status_count)
		, fb_pkt_count(fb_pkt_count)
		, reference_time_micros(64 * 1000 * static_cast<int64_t>(reference_time))
		, fb_pkt_count_expanded(fb_pkt_count)
	{
	}
};

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

	void update(const std::shared_ptr<FeedbackHeader>& header);

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
	uint16_t mMaxSeq;
	std::unique_ptr<PublishPacket[]> mHistory;
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

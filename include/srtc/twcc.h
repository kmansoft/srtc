#pragma once

#include <cstdint>
#include <ctime>
#include <list>
#include <memory>
#include <optional>

#include "srtc/util.h"
#include "srtc/temp_buffer.h"

namespace srtc
{
class RtpPacket;
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
		, reference_time_micros(64 * 1000 * reference_time)
		, fb_pkt_count_expanded(fb_pkt_count)
	{
	}
};

// A history of such headers

class FeedbackHeaderHistory
{
public:
	FeedbackHeaderHistory();
	~FeedbackHeaderHistory();

	void save(const std::shared_ptr<FeedbackHeader>& header);

	[[nodiscard]] uint32_t getPacketCount() const;

private:
	uint32_t mPacketCount;
	std::list<std::shared_ptr<FeedbackHeader>> mHistory;

	uint16_t mLastFbPktCount = 0;
	uint16_t mLastFbPktCountExpanded = 0;
};

// Status of a single RTP packet

struct PacketStatus {
	int64_t sent_time_micros;
	int64_t received_time_micros;

	int32_t sent_delta_micros;
	int32_t received_delta_micros;

	uint16_t payload_size;
	uint16_t generated_size;
	uint16_t encrypted_size;

	uint16_t seq;
	uint16_t nack_count;

	uint8_t reported_status;

	bool reported_as_not_received;
	bool reported_checked;
};

// A history of such packets

class PacketStatusHistory
{
public:
	PacketStatusHistory();
	~PacketStatusHistory();

	void save(uint16_t seq, size_t payloadSize, size_t generatedSize, size_t encryptedSize);

	// may return nullptr
	[[nodiscard]] PacketStatus* get(uint16_t seq) const;

	void update(const std::shared_ptr<FeedbackHeader>& header);

	[[nodiscard]] uint32_t getPacketCount() const;
	[[nodiscard]] bool isDataRecentEnough() const;
	[[nodiscard]] float getPacketsLostPercent() const;
	[[nodiscard]] float getRttMillis() const;
	[[nodiscard]] float getBandwidthKbitPerSecond() const;

private:
	uint16_t mMinSeq;
	uint16_t mMaxSeq;
	std::unique_ptr<PacketStatus[]> mHistory;
	Filter<float> mPacketsLostFilter;
	Filter<float> mRttFilter;
	Filter<float> mBandwidthFilter;
	int64_t mLastUpdated;

	struct ReceivedPacket {
		uint64_t received_time_micros;
		uint16_t size;
	};
	DynamicTempBuffer<ReceivedPacket> mReceivedPacketBuf;

	static int compare_received_packet(const void* p1, const void* p2);

	[[nodiscard]] PacketStatus* findMostRecentReceivedPacket() const;
};

} // namespace srtc::twcc

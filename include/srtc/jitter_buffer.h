#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/encoded_frame.h"
#include "srtc/srtc.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace srtc
{

class Depacketizer;
class Track;
class RtpPacket;
class RtcpPacket;

// Extends sequential values to 64 bits on rollover, used for SEQ and RTP timestamps

template <typename T>
class ExtendedValue
{
public:
	ExtendedValue();

	std::optional<uint64_t> extend(T src);

private:
	uint64_t mRollover;
	std::optional<T> mLast;
};

// Jitter buffer, has a fixed max capacity (number of packets) and duration, for now

class JitterBuffer
{
public:
	JitterBuffer(const std::shared_ptr<Track>& track,
				 const std::shared_ptr<Depacketizer>& depacketizer,
				 size_t capacity,
				 std::chrono::milliseconds length,
				 std::chrono::milliseconds nackDelay);

	~JitterBuffer();

	[[nodiscard]] std::shared_ptr<Track> getTrack() const;

	// Adding received packets
	void consume(const std::shared_ptr<RtpPacket>& packet);

	// Processing
	[[nodiscard]] int getTimeoutMillis(int defaultTimeout) const;
	[[nodiscard]] std::vector<std::shared_ptr<EncodedFrame>> processDeque();
	[[nodiscard]] std::vector<uint16_t> processNack();

private:
	struct Item {
		std::chrono::steady_clock::time_point when_received;
		std::chrono::steady_clock::time_point when_dequeue;
		std::chrono::steady_clock::time_point when_nack_request;
		std::chrono::steady_clock::time_point when_nack_abandon;

		bool received = false;
		bool nack_needed = false;

		PacketKind kind = PacketKind::Standalone;

		uint64_t seq_ext = 0;
		uint64_t rtp_timestamp_ext = 0; // only when received

		ByteBuffer payload;
	};

	[[nodiscard]] bool findMultiPacketSequence(uint64_t& outEnd);

	const std::shared_ptr<Track> mTrack;
	const std::shared_ptr<Depacketizer> mDepacketizer;
	const size_t mCapacity;
	const size_t mCapacityMask;
	const std::chrono::milliseconds mLength;
	const std::chrono::milliseconds mNackDelay;

	Item** mItemList;
	uint64_t mMinSeq;
	uint64_t mMaxSeq;

	ExtendedValue<uint16_t> mExtValueSeq;
	ExtendedValue<uint32_t> mExtValueRtpTimestamp;

	std::chrono::steady_clock::time_point mBaseTime;
	uint64_t mBaseRtpTimestamp;
};

} // namespace srtc
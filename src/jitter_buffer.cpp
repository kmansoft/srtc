#include "srtc/jitter_buffer.h"
#include "srtc/depacketizer.h"
#include "srtc/logging.h"
#include "srtc/rtp_packet.h"
#include "srtc/srtc.h"
#include "srtc/track.h"

#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <limits>

#define LOG(level, ...) srtc::log(level, "JitterBuffer", __VA_ARGS__)

namespace srtc
{

// Extended Value

template <typename T>
ExtendedValue<T>::ExtendedValue()
	: mRollover(0)
{
}

template <typename T>
std::optional<uint64_t> ExtendedValue<T>::extend(T src)
{
	if (!mLast.has_value()) {
		mLast = src;
		return src;
	}

	constexpr auto margin = std::numeric_limits<T>::max() / 10;
	constexpr auto max = std::numeric_limits<T>::max();

	if (mLast.value() >= max - margin && src <= margin) {
		// Rollover
		mRollover += static_cast<uint64_t>(max) + 1;
		mLast = src;
		return mRollover | src;
	} else if (mLast.value() <= margin && src >= max - margin) {
		// We just had a rollover, and the new value wants to go backwards
		if (mRollover == 0) {
			// But we can't
			return std::nullopt;
		}

		const auto val = (mRollover - (static_cast<uint64_t>(max) + 1)) | src;
		return val;
	} else {
		mLast = src;
		return mRollover | src;
	}
}

template class ExtendedValue<uint16_t>;
template class ExtendedValue<uint32_t>;

// Jitter Buffer

JitterBuffer::JitterBuffer(const std::shared_ptr<Track>& track,
						   const std::shared_ptr<Depacketizer>& depacketizer,
						   size_t capacity,
						   std::chrono::milliseconds length,
						   std::chrono::milliseconds nackDelay)
	: mTrack(track)
	, mDepacketizer(depacketizer)
	, mCapacity(capacity)
	, mCapacityMask(capacity - 1)
	, mLength(length)
	, mNackDelay(nackDelay)
	, mItemList(nullptr)
	, mMinSeq(0)
	, mMaxSeq(0)
	, mBaseTime(std::chrono::steady_clock::time_point::min())
	, mBaseRtpTimestamp(0)
{
	assert((mCapacity & mCapacityMask) == 0 && "capacity should be a power of 2");
}

JitterBuffer::~JitterBuffer()
{
	if (mItemList) {
		for (uint64_t seq = mMinSeq; seq < mMaxSeq; seq += 1) {
			const auto index = seq & (mCapacity - 1);
			delete mItemList[index];
		}

		delete[] mItemList;
	}
}

std::shared_ptr<Track> JitterBuffer::getTrack() const
{
	return mTrack;
}

void JitterBuffer::consume(const std::shared_ptr<RtpPacket>& packet)
{
	assert(mTrack == packet->getTrack());

	auto seq = packet->getSequence();
	auto payload = packet->movePayload();

	if (packet->getSSRC() == mTrack->getRtxSSRC() && packet->getPayloadId() == mTrack->getRtxPayloadId()) {
		// Unwrap RTX
		if (payload.size() < 2) {
			LOG(SRTC_LOG_E, "RTX payload is less than 2 bytes, can't be");
			return;
		}

		ByteReader reader(payload);
		seq = reader.readU16();

		payload = { payload.data() + 2, payload.size() - 2 };
	}

	// Extend
	const auto seq_ext = mExtValueSeq.extend(seq);
	if (!seq_ext) {
		LOG(SRTC_LOG_E, "Cannot extend the sequence number");
		return;
	}
	const auto rtp_timestamp_ext = mExtValueRtpTimestamp.extend(packet->getTimestamp());
	if (!rtp_timestamp_ext) {
		LOG(SRTC_LOG_E, "Cannot extend the rtp timestamp");
		return;
	}

	if (mItemList == nullptr) {
		// First packet
		mMinSeq = seq_ext.value();
		mMaxSeq = mMinSeq + 1;
		mItemList = new Item*[mCapacity];
		mBaseTime = std::chrono::steady_clock::now();
		mBaseRtpTimestamp = rtp_timestamp_ext.value();

		const auto item = new Item;
		mItemList[seq_ext.value() & mCapacityMask] = item;
	} else if (seq_ext.value() + mCapacity / 10 < mMinSeq) {
		// Out of range, much less than min
		LOG(SRTC_LOG_E,
			"The new packet's sequence number %" PRIu64 " (0x%" PRIx64 ") is too late",
			seq_ext.value(),
			seq_ext.value());
		return;
	} else if (seq_ext.value() - mCapacity / 10 > mMaxSeq) {
		// Out of range, much greater than max
		LOG(SRTC_LOG_E,
			"The new packet's sequence number %" PRIu64 " (0x%" PRIx64 ") is too early",
			seq_ext.value(),
			seq_ext.value());
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	const auto rtp_timestamp_delta = rtp_timestamp_ext.value() - mBaseRtpTimestamp;
	const auto time_delta = std::chrono::milliseconds(1000 * rtp_timestamp_delta / mTrack->getClockRate());
	const auto packet_time = mBaseTime + time_delta;
	const auto when_dequeue = packet_time + mLength;
	const auto when_nack_request = now + mNackDelay;

	Item* item = nullptr;

	if (seq_ext.value() < mMinSeq) {
		// Before min
		if (mMaxSeq - seq_ext.value() > mCapacity) {
			LOG(SRTC_LOG_E,
				"The new packet with sequence number %" PRIu64 " (%" PRIx64 ") would exceed the capacity",
				seq_ext.value(),
				seq_ext.value());
			return;
		}

		while (seq_ext.value() + 1 < mMinSeq) {
			mMinSeq -= 1;

			const auto spacer = new Item;
			spacer->when_received = std::chrono::steady_clock::time_point::min();
			spacer->when_dequeue = std::chrono::steady_clock::time_point::max();
			spacer->when_nack_request = when_nack_request;
			spacer->when_nack_abandon = when_dequeue;

			spacer->received = false;
			spacer->nack_needed = true;

			spacer->seq_ext = mMinSeq;
			spacer->rtp_timestamp_ext = 0;

			const auto index = spacer->seq_ext & mCapacityMask;
			mItemList[index] = spacer;
		}

		mMinSeq -= 1;
		assert(mMinSeq == seq_ext.value());

		const auto index = seq_ext.value() & mCapacityMask;
		item = new Item;
		mItemList[index] = item;
	} else if (seq_ext >= mMaxSeq) {
		// Above max
		if (seq_ext.value() - mMinSeq > mCapacity) {
			LOG(SRTC_LOG_E,
				"The new packet with sequence number %" PRIu64 " (%" PRIx64 ") would exceed the capacity",
				seq_ext.value(),
				seq_ext.value());
			return;
		}

		while (mMaxSeq <= seq_ext.value() - 1) {
			mMaxSeq += 1;

			const auto spacer = new Item;
			spacer->when_received = std::chrono::steady_clock::time_point::min();
			spacer->when_dequeue = std::chrono::steady_clock::time_point::max();
			spacer->when_nack_request = when_nack_request;
			spacer->when_nack_abandon = when_dequeue;

			spacer->received = false;
			spacer->nack_needed = true;

			spacer->seq_ext = mMaxSeq;
			spacer->rtp_timestamp_ext = 0;

			const auto index = spacer->seq_ext & mCapacityMask;
			mItemList[index] = spacer;
		}

		mMaxSeq += 1;
		assert(mMaxSeq - 1 == seq_ext.value());

		const auto index = seq_ext.value() & mCapacityMask;
		item = new Item;
		mItemList[index] = item;
	} else {
		// Somewhere in the middle, we should already have an item there
		const auto index = seq_ext.value() & mCapacityMask;
		item = mItemList[index];
		assert(item);
	}

	item->when_received = now;
	item->when_dequeue = when_dequeue;
	item->when_nack_request = when_nack_request;
	item->when_nack_abandon = when_dequeue;

	item->received = true;
	item->nack_needed = false;

	item->kind = mDepacketizer->getPacketKind(payload);

	item->seq_ext = seq_ext.value();
	item->rtp_timestamp_ext = rtp_timestamp_ext.value();

	item->payload = std::move(payload);
}

int JitterBuffer::getTimeoutMillis(int defaultTimeout) const
{
	if (!mItemList) {
		return defaultTimeout;
	}

	const auto now = std::chrono::steady_clock::now();

	// We add packets on the Max end and consume them from the Min end
	for (auto seq = mMinSeq; seq < mMaxSeq; seq += 1) {
		const auto index = seq & mCapacityMask;
		const auto item = mItemList[index];
		assert(item);

		// Depacketization
		if (item->received) {
			return static_cast<int>(
				std::chrono::duration_cast<std::chrono::milliseconds>(item->when_dequeue - now).count());
		}

		if (item->when_dequeue > now + std::chrono::milliseconds(defaultTimeout)) {
			break;
		}
	}

	return defaultTimeout;
}

std::vector<std::shared_ptr<EncodedFrame>> JitterBuffer::dequeue()
{
	std::vector<std::shared_ptr<EncodedFrame>> result;

	if (!mItemList || mMinSeq == mMaxSeq) {
		return result;
	}

	const auto now = std::chrono::steady_clock::now();

	// We add packets on the Max end and consume them from the Min end
	while (mMinSeq < mMaxSeq) {
		const auto seq = mMinSeq;
		const auto index = seq & mCapacityMask;
		const auto item = mItemList[index];
		assert(item);

		if (item->received && item->when_dequeue <= now) {
			if (item->kind == PacketKind::Standalone) {
				// A standalone packet, which is ready to be extracted, possibly into multiple frames
				auto frameList = mDepacketizer->extract(item->payload);

				while (!frameList.empty()) {
					const auto frame = std::make_shared<EncodedFrame>();

					frame->track = mTrack;
					frame->seq_ext = item->seq_ext;
					frame->rtp_timestamp_ext = item->rtp_timestamp_ext;
					frame->data = std::move(frameList.front());

					result.push_back(frame);

					frameList.pop_front();
				}

				mItemList[index] = nullptr;
				mMinSeq += 1;

				delete item;
			} else if (item->kind == PacketKind::Start) {
				// Start of a multi-packet sequence
				uint64_t maxSeq = 0;
				if (findMultiPacketSequence(maxSeq)) {

					std::vector<ByteBuffer*> bufList;
					for (auto extract_seq = seq; extract_seq <= maxSeq; extract_seq += 1) {
						const auto extract_index = extract_seq & mCapacityMask;
						auto extract_item = mItemList[extract_index];
						assert(extract_item);
						bufList.push_back(&extract_item->payload);
					}

					// Extract, possibly into multiple frames (theoretical)
					auto frameList = mDepacketizer->extract(bufList);

					while (!frameList.empty()) {
						const auto frame = std::make_shared<EncodedFrame>();

						frame->track = mTrack;
						frame->seq_ext = item->seq_ext;
						frame->rtp_timestamp_ext = item->rtp_timestamp_ext;
						frame->data = std::move(frameList.front());

						result.push_back(frame);

						frameList.pop_front();
					}

					for (auto cleanup_seq = seq; cleanup_seq <= maxSeq; cleanup_seq += 1) {
						const auto cleanup_index = cleanup_seq & mCapacityMask;
						delete mItemList[cleanup_index];
						mItemList[cleanup_index] = nullptr;
					}

					mMinSeq = maxSeq + 1;
				} else {
					mItemList[index] = nullptr;
					mMinSeq += 1;

					delete item;
				}
			} else {
				// We cannot extract this - delete and keep going
				mItemList[index] = nullptr;
				mMinSeq += 1;

				delete item;
			}
		} else if (item->when_nack_abandon <= now) {
			// A nack that was never received - delete and keep going
			mItemList[index] = nullptr;
			mMinSeq += 1;

			delete item;
		} else {
			break;
		}
	}

	return result;
}

bool JitterBuffer::findMultiPacketSequence(uint64_t& outEnd)
{
	auto index = (mMinSeq) & mCapacityMask;
	auto item = mItemList[index];
	assert(item);
	assert(item->received);
	assert(item->kind == PacketKind::Start);

	for (auto seq = mMinSeq + 1; seq < mMaxSeq; seq += 1) {
		index = seq & mCapacityMask;
		item = mItemList[index];
		assert(item);

		if (!item->received) {
			break;
		} else if (item->kind == PacketKind::End) {
			outEnd = seq;
			return true;
		} else if (item->kind != PacketKind::Middle) {
			for (auto delete_seq = mMinSeq; delete_seq <= seq; delete_seq += 1) {
				const auto delete_index = delete_seq & mCapacityMask;
				const auto delete_item = mItemList[delete_index];
				delete delete_item;
				mItemList[delete_index] = nullptr;
			}

			mMinSeq = seq + 1;
			break;
		}
	}

	return false;
}

} // namespace srtc

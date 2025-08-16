#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/encoded_frame.h"
#include "srtc/extended_value.h"
#include "srtc/jitter_buffer_item.h"
#include "srtc/pool_allocator.h"
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
    void freeEverything();
    [[nodiscard]] JitterBufferItem* newItem();
    void deleteItem(JitterBufferItem* item);

    [[nodiscard]] JitterBufferItem* newLostItem(const std::chrono::steady_clock::time_point& when_nack_request,
                                                const std::chrono::steady_clock::time_point& when_nack_abandon,
                                                uint64_t seq_ext);

    void extractBufferList(std::vector<const JitterBufferItem*>& out, uint64_t start, uint64_t max);
    void deleteItemList(uint64_t start, uint64_t max);
    void appendToResult(std::vector<std::shared_ptr<srtc::EncodedFrame>>& result,
                        JitterBufferItem* item,
                        JitterBufferItem* last,
                        const std::chrono::steady_clock::time_point& now,
                        std::vector<srtc::ByteBuffer>& list);

    [[nodiscard]] bool findMultiPacketSequence(uint64_t& outEnd);
    [[nodiscard]] bool findNextToDequeue(const std::chrono::steady_clock::time_point& now);

    const std::shared_ptr<Track> mTrack;
    const std::shared_ptr<Depacketizer> mDepacketizer;
    const size_t mCapacity;
    const size_t mCapacityMask;
    const std::chrono::milliseconds mLength;
    const std::chrono::milliseconds mNackDelay;

    PoolAllocator<JitterBufferItem> mItemAllocator;

    std::chrono::steady_clock::time_point mLastPacketTime;

    JitterBufferItem** mItemList;
    uint64_t mMinSeq;
    uint64_t mMaxSeq;

    ExtendedValue<uint16_t> mExtValueSeq;
    ExtendedValue<uint32_t> mExtValueRtpTimestamp;

    std::chrono::steady_clock::time_point mBaseTime;
    uint64_t mBaseRtpTimestamp;

    std::vector<ByteBuffer> mTempFrameList;
    std::vector<const JitterBufferItem*> mTempBufferList;

    std::optional<uint64_t> mLastFrameTimeStamp;
};

} // namespace srtc
#pragma once

#include <cstdint>
#include <memory>

#include "srtc/random_generator.h"

namespace srtc
{

class RtpPacketSource
{
public:
    RtpPacketSource(uint32_t ssrc, uint8_t payloadId);
    ~RtpPacketSource();

    [[nodiscard]] uint32_t getSSRC() const;
    [[nodiscard]] uint8_t getPayloadId() const;

    [[nodiscard]] std::pair<uint32_t, uint16_t> getNextSequence();

    void clear();

private:
    const uint32_t mSSRC;
    const uint8_t mPayloadId;
    RandomGenerator<uint32_t> mRandom;
    uint32_t mGeneratedCount;
    uint32_t mRollover;
    uint16_t mNextSequence;
};

} // namespace srtc

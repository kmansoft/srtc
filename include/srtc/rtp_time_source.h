#pragma once

#include <chrono>
#include <optional>
#include <cstdint>

#include "srtc/random_generator.h"

namespace srtc
{

class RtpTimeSource
{
public:
    explicit RtpTimeSource(uint32_t clockRate);
    ~RtpTimeSource();

    [[nodiscard]] uint32_t getFrameTimestamp(int64_t pts_usec);
    [[nodiscard]] uint32_t getCurrentTimestamp() const;

private:
    RandomGenerator<uint32_t> mRandom;
    const uint32_t mClockRate;

    std::optional<int64_t> mCurrPts;
    std::chrono::steady_clock::time_point mCurrTime;
    uint32_t mCurrRtp;
};

} // namespace srtc

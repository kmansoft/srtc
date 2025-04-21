#pragma once

#include <cstdint>
#include <chrono>

#include "srtc/random_generator.h"

namespace srtc {

class RtpTimeSource {
public:
    RtpTimeSource(uint32_t clockRate);
    ~RtpTimeSource();

    [[nodiscard]] uint32_t getCurrTimestamp();

private:
    RandomGenerator<uint32_t> mRandom;
    const uint32_t mClockRate;
    const std::chrono::steady_clock::time_point mClockBaseTime;
    const uint32_t mClockBaseValue;
};

}
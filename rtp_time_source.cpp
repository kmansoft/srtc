#include "srtc/rtp_time_source.h"

namespace srtc {

RtpTimeSource::RtpTimeSource(uint32_t clockRate)
    : mClockRate(clockRate)
    , mRandom(0, std::numeric_limits<int32_t>::max())
    , mClockBaseValue(mRandom.next())
    , mClockBaseTime(std::chrono::steady_clock::now())
{
}

RtpTimeSource::~RtpTimeSource() = default;

uint32_t RtpTimeSource::getCurrTimestamp()
{
    const auto now = std::chrono::steady_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now - mClockBaseTime).count();

    const auto timestamp = static_cast<uint32_t>(millis * mClockRate / 1000 + mClockBaseValue);
    return timestamp;
}

}
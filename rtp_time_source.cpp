#include "srtc/rtp_time_source.h"

namespace srtc
{

RtpTimeSource::RtpTimeSource(uint32_t clockRate)
    : mRandom(0, std::numeric_limits<int32_t>::max())
    , mClockRate(clockRate)
	, mClockBaseTime(std::chrono::steady_clock::now())
    , mClockBaseValue(mRandom.next())
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

} // namespace srtc
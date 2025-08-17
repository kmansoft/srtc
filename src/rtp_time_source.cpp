#include "srtc/rtp_time_source.h"
#include "srtc/logging.h"

namespace srtc
{

RtpTimeSource::RtpTimeSource(uint32_t clockRate)
    : mRandom(0, std::numeric_limits<int32_t>::max())
    , mClockRate(clockRate)
    , mCurrTime(std::chrono::steady_clock::now())
    , mCurrRtp(mRandom.next())
{
}

RtpTimeSource::~RtpTimeSource() = default;

uint32_t RtpTimeSource::getFrameTimestamp(int64_t pts)
{
    if (!mCurrPts.has_value()) {
        mCurrPts = pts;
    }

    const auto elapsed_usec = pts - mCurrPts.value();
    if (elapsed_usec < 0) {
        // A client app bug, do something sensible
        return mCurrRtp;
    }

    mCurrRtp += elapsed_usec * mClockRate / 1000000;
    mCurrPts = pts;
    mCurrTime = std::chrono::steady_clock::now();

    return mCurrRtp;
}

uint32_t RtpTimeSource::getCurrentTimestamp() const
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_usec = std::chrono::duration_cast<std::chrono::microseconds>(now - mCurrTime).count();
    return mCurrRtp + elapsed_usec * mClockRate / 1000000;
}

} // namespace srtc

#include "srtc/packetizer.h"
#include "srtc/packetizer_h264.h"

namespace srtc {

Packetizer::Packetizer()
    : mRandom(0, 10000)
    , mSequence(mRandom.next())
    , mClockBaseTime(std::chrono::steady_clock::now())
    , mClockBaseValue(mRandom.next() % 10000)
{
}

Packetizer::~Packetizer() = default;

std::pair<std::shared_ptr<Packetizer>, Error> Packetizer::makePacketizer(const Codec& codec)
{
    switch (codec) {
        case Codec::H264:
            return { std::make_shared<PacketizerH264>(), Error::OK };
        default:
            return { nullptr, { Error::Code::InvalidData, "Unsupported codec type" }};
    }
}

uint16_t Packetizer::getSequence()
{
    return mSequence++;
}

uint32_t Packetizer::getTimestamp()
{
    // RTP is 90,000 HZ which is 90 ticks per millisecond
    const auto now = std::chrono::steady_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now - mClockBaseTime).count();

    if (mLastMillis.has_value() && mLastMillis.value() == millis) {
        return ++mLastTimestamp;
    }

    mLastMillis = millis;
    mLastTimestamp = static_cast<uint32_t>(millis * 90 + mClockBaseValue + mRandom.next() % 10);
    return mLastTimestamp;
}

}

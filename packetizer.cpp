#include "srtc/packetizer.h"
#include "srtc/packetizer_h264.h"
#include "srtc/packetizer_opus.h"

namespace srtc {

Packetizer::Packetizer()
    : mRandom(0, 10000)
    , mClockBaseTime(std::chrono::steady_clock::now())
    , mClockBaseValue(mRandom.next() % 10000)
    , mLastTimestamp(0L)
{
}

Packetizer::~Packetizer() = default;

void Packetizer::setCodecSpecificData([[maybe_unused]] const std::vector<ByteBuffer>& csd)
{
}

bool Packetizer::isKeyFrame(const ByteBuffer& frame) const
{
    return false;
}

std::pair<std::shared_ptr<Packetizer>, Error> Packetizer::makePacketizer(const Codec& codec)
{
    switch (codec) {
        case Codec::H264:
            return { std::make_shared<PacketizerH264>(), Error::OK };
        case Codec::Opus:
            return { std::make_shared<PacketizerOpus>(), Error::OK };
        default:
            return { nullptr, { Error::Code::InvalidData, "Unsupported codec type" }};
    }
}

uint32_t Packetizer::getNextTimestamp(int clockRateKHz)
{
    const auto now = std::chrono::steady_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now - mClockBaseTime).count();

    if (mLastMillis.has_value() && mLastMillis.value() == millis) {
        return ++mLastTimestamp;
    }

    mLastMillis = millis;
    mLastTimestamp = static_cast<uint32_t>(millis * clockRateKHz + mClockBaseValue + mRandom.next() % 10);
    return mLastTimestamp;
}

}

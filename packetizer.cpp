#include "srtc/packetizer.h"
#include "srtc/packetizer_h264.h"

namespace srtc {

Packetizer::Packetizer()
    : mRandom(0, 10000)
    , mClockBaseTime(std::chrono::steady_clock::now())
    , mClockBaseValue(mRandom.next() % 10000)
    , mSequence(mRandom.next())
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

uint32_t Packetizer::getClockTimestamp()
{
    // RTP is 90,000 HZ which is 90 ticks per millisecond or 90 ticks per 1000 microseconds
    const auto now = std::chrono::steady_clock::now();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now - mClockBaseTime).count();
    return static_cast<uint32_t>((micros * 90) / 1000 + mClockBaseValue + mRandom.next() % 50);
}

uint32_t Packetizer::getSequence()
{
    return mSequence++;
}

}

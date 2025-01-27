#pragma once

#include "srtc/error.h"
#include "srtc/srtc.h"
#include "srtc/random_generator.h"
#include "srtc/rtp_packet.h"

#include <list>
#include <memory>
#include <utility>
#include <chrono>

namespace srtc {

class Track;
class ByteBuffer;
class RtpPacket;

class Packetizer {
public:
    Packetizer();
    virtual ~Packetizer();

    virtual void setCodecSpecificData(const std::vector<ByteBuffer>& csd);
    virtual std::list<std::shared_ptr<RtpPacket>> generate(const std::shared_ptr<Track>& track,
                                                           const ByteBuffer& frame) = 0;

    static std::pair<std::shared_ptr<Packetizer>, Error> makePacketizer(const Codec& codec);

    [[nodiscard]] uint16_t getNextSequence();
    [[nodiscard]] uint32_t getNextTimestamp(int clockRateKHz);

private:
    RandomGenerator<uint32_t> mRandom;
    uint16_t mSequence;
    const std::chrono::steady_clock::time_point mClockBaseTime;
    const uint32_t mClockBaseValue;

    std::optional<long long> mLastMillis;
    uint32_t mLastTimestamp;
};

}

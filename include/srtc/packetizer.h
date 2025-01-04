#pragma once

#include "srtc/error.h"
#include "srtc/srtc.h"
#include "srtc/random_generator.h"

#include <utility>
#include <chrono>

namespace srtc {

class ByteBuffer;

class Packetizer {
public:
    Packetizer();
    virtual ~Packetizer();

    virtual void setCodecSpecificData(const std::vector<ByteBuffer>& csd) = 0;
    virtual void process(const ByteBuffer& frame) = 0;

    static std::pair<std::shared_ptr<Packetizer>, Error> makePacketizer(const Codec& codec);

    [[nodiscard]] uint32_t getClockTimestamp();
    [[nodiscard]] uint32_t getSequence();

private:
    RandomGenerator<uint32_t> mRandom;
    const std::chrono::steady_clock::time_point mClockBaseTime;
    const uint32_t mClockBaseValue;
    uint32_t mSequence;
};

}

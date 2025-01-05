#pragma once

#include "srtc/error.h"
#include "srtc/srtc.h"
#include "srtc/random_generator.h"
#include "srtc/rtp_packet.h"

#include <list>
#include <utility>
#include <chrono>

namespace srtc {

class ByteBuffer;
class RtpPacket;

class Packetizer {
public:
    Packetizer();
    virtual ~Packetizer();

    virtual void setCodecSpecificData(const std::vector<ByteBuffer>& csd) = 0;
    virtual std::list<RtpPacket> generate(uint8_t payloadType,
                                          uint32_t ssrc,
                                          const ByteBuffer& frame) = 0;

    static std::pair<std::shared_ptr<Packetizer>, Error> makePacketizer(const Codec& codec);

    [[nodiscard]] uint16_t getNextSequence();
    [[nodiscard]] uint32_t getNextTimestamp();

private:
    RandomGenerator<uint32_t> mRandom;
    uint16_t mSequence;
    const std::chrono::steady_clock::time_point mClockBaseTime;
    const uint32_t mClockBaseValue;

    std::optional<long long> mLastMillis;
    uint32_t mLastTimestamp;
};

}

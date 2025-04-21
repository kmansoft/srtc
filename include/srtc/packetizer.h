#pragma once

#include "srtc/error.h"
#include "srtc/srtc.h"
#include "srtc/random_generator.h"
#include "srtc/rtp_packet.h"
#include "srtc/optional.h"

#include <list>
#include <memory>
#include <utility>
#include <chrono>

namespace srtc {

class Track;
class ByteBuffer;
class RtpPacket;
class RtpPacketSource;
class RtpExtension;

class Packetizer {
public:
    Packetizer(const std::shared_ptr<Track>& track);
    virtual ~Packetizer();

    virtual void setCodecSpecificData(const std::vector<ByteBuffer>& csd);
    virtual bool isKeyFrame(const ByteBuffer& frame) const;
    virtual std::list<std::shared_ptr<RtpPacket>> generate(const RtpExtension& extension,
                                                           bool addExtensionToAllPackets,
                                                           const ByteBuffer& frame) = 0;

    static std::pair<std::shared_ptr<Packetizer>, Error> makePacketizer(const std::shared_ptr<Track>& track);

    [[nodiscard]] std::shared_ptr<Track> getTrack() const;
    [[nodiscard]] uint32_t getNextTimestamp(int clockRateKHz);

private:
    const std::shared_ptr<Track> mTrack;

    RandomGenerator<uint32_t> mRandom;
    const std::chrono::steady_clock::time_point mClockBaseTime;
    const uint32_t mClockBaseValue;

    srtc::optional<long long> mLastMillis;
    uint32_t mLastTimestamp;
};

}

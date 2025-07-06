#pragma once

#include "srtc/error.h"
#include "srtc/random_generator.h"
#include "srtc/rtp_packet.h"
#include "srtc/srtc.h"

#include <chrono>
#include <list>
#include <memory>
#include <utility>

namespace srtc
{

class Track;
class ByteBuffer;
class RtpPacket;
class RtpPacketSource;
class RtpExtension;
class RtpExtensionSource;

class Packetizer
{
public:
    explicit Packetizer(const std::shared_ptr<Track>& track);
    virtual ~Packetizer();

    virtual void setCodecSpecificData(const std::vector<ByteBuffer>& csd);
	[[nodiscard]] virtual bool isKeyFrame(const ByteBuffer& frame) const;
    virtual std::list<std::shared_ptr<RtpPacket>> generate(const std::shared_ptr<RtpExtensionSource>& simulcast,
                                                           const std::shared_ptr<RtpExtensionSource>& twcc,
                                                           size_t mediaProtectionOverhead,
                                                           const ByteBuffer& frame) = 0;

    static std::pair<std::shared_ptr<Packetizer>, Error> make(const std::shared_ptr<Track>& track);

    [[nodiscard]] std::shared_ptr<Track> getTrack() const;

private:
    const std::shared_ptr<Track> mTrack;
};

} // namespace srtc

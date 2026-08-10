#pragma once

#include "srtc/error.h"
#include "srtc/rtp_packet.h"
#include "srtc/srtc.h"

#include <memory>
#include <utility>
#include <vector>

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

    static std::pair<std::shared_ptr<Packetizer>, Error> make(const std::shared_ptr<Track>& track);
    static size_t getBasicPacketSize(size_t mediaProtectionOverhead);

    virtual void setCodecSpecificData(const std::vector<ByteBuffer>& csd);

    [[nodiscard]] virtual bool isKeyFrame(const ByteBuffer& frame) const;
    [[nodiscard]] virtual std::vector<std::shared_ptr<RtpPacket>> generate(
        const std::vector<std::shared_ptr<RtpExtensionSource>>& extensionSourceList,
        size_t mediaProtectionOverhead,
        int64_t pts_usec,
        const ByteBuffer& frame) = 0;

    [[nodiscard]] std::shared_ptr<Track> getTrack() const;

    static RtpExtension buildExtension(const std::shared_ptr<Track>& track,
                                       const std::vector<std::shared_ptr<RtpExtensionSource>>& extensionSourceList,
                                       bool isKeyFrame,
                                       unsigned int packetNumber);

private:
    const std::shared_ptr<Track> mTrack;
};

} // namespace srtc

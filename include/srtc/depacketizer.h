#pragma once

#include "srtc/error.h"
#include "srtc/srtc.h"

#include <memory>
#include <vector>

namespace srtc
{

class Track;
class ByteBuffer;

class Depacketizer
{
public:
    explicit Depacketizer(const std::shared_ptr<Track>& track);
    virtual ~Depacketizer();

    [[nodiscard]] virtual PacketKind getPacketKind(const ByteBuffer& packet) = 0;

    virtual void reset() = 0;

    virtual void extract(std::vector<ByteBuffer>& out, ByteBuffer& packet) = 0;
    virtual void extract(std::vector<ByteBuffer>& out, const std::vector<ByteBuffer*>& packetList) = 0;

    static std::pair<std::shared_ptr<Depacketizer>, Error> make(const std::shared_ptr<Track>& track);

    [[nodiscard]] std::shared_ptr<Track> getTrack() const;

private:
    const std::shared_ptr<Track> mTrack;
};

} // namespace srtc
#include "srtc/depacketizer_video.h"
#include "srtc/track.h"

#include <cassert>

namespace srtc
{

DepacketizerVideo::DepacketizerVideo(const std::shared_ptr<Track>& track)
    : Depacketizer(track)
{
    assert(track->getMediaType() == MediaType::Video);
}

DepacketizerVideo::~DepacketizerVideo() = default;

PacketKind DepacketizerVideo::getPacketKind(const ByteBuffer& payload, bool marker) const
{
    if (isFrameStart(payload)) {
        if (marker) {
            return PacketKind::Standalone;
        }
        return PacketKind::Start;
    }
    if (marker) {
        return PacketKind::End;
    }
    return PacketKind::Middle;
}

} // namespace srtc
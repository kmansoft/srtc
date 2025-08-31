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

} // namespace srtc
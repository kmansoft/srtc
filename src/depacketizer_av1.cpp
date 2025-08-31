#include "srtc/depacketizer_av1.h"

#include "srtc/logging.h"
#include "srtc/util.h"

#define LOG(level, ...) srtc::log(level, "DepacketizerAV1", __VA_ARGS__)

namespace srtc
{

DepacketizerAV1::DepacketizerAV1(const std::shared_ptr<Track>& track)
    : DepacketizerVideo(track)
    , mSeenKeyFrame(false)
{
}

DepacketizerAV1::~DepacketizerAV1() = default;

void DepacketizerAV1::reset()
{
    mSeenKeyFrame = false;
}

void DepacketizerAV1::extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList)
{
    out.clear();

}

bool DepacketizerAV1::isFrameStart(const ByteBuffer& payload) const
{
    return false;
}

void DepacketizerAV1::extractImpl(std::vector<ByteBuffer>& out, const JitterBufferItem* packet, ByteBuffer&& frame)
{
}

} // namespace srtc
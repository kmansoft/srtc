#include "srtc/depacketizer_vp9.h"
#include "srtc/codec_vp9.h"
#include "srtc/logging.h"
#include "srtc/track.h"

#include <cassert>

#define LOG(level, ...) srtc::log(level, "DepacketizerVP9", __VA_ARGS__)

namespace srtc
{

DepacketizerVP9::DepacketizerVP9(const std::shared_ptr<Track>& track)
    : DepacketizerVideo(track)
    , mSeenKeyFrame(false)
{
    assert(track->getCodec() == Codec::VP9);
}

DepacketizerVP9::~DepacketizerVP9() = default;

void DepacketizerVP9::reset()
{
    mSeenKeyFrame = false;
}

void DepacketizerVP9::extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList)
{
    out.clear();

    ByteBuffer buf;
    ByteWriter w(buf);

    for (const auto packet : packetList) {
        // https://www.rfc-editor.org/rfc/rfc9628#section-4
        vp9::PayloadDescriptor desc = {};
        const uint8_t* payloadData = nullptr;
        size_t payloadSize = 0;

        if (!vp9::parsePayloadDescriptor(
                packet->payload.data(), packet->payload.size(), desc, payloadData, payloadSize)) {
            return;
        }

        w.write(payloadData, payloadSize);
    }

    if (buf.empty()) {
        return;
    }

    if (!mSeenKeyFrame) {
        if (vp9::isKeyFrame(buf.data(), buf.size())) {
            mSeenKeyFrame = true;
        } else {
            LOG(SRTC_LOG_V, "Not emitting a non-key frame until there is a keyframe");
            return;
        }
    }

    if (packetList.back()->marker) {
        out.push_back(std::move(buf));
    }
}

bool DepacketizerVP9::isFrameStart(const ByteBuffer& payload) const
{
    if (!payload.empty()) {
        // B bit is bit 3 of the first descriptor byte
        return (payload.front() & 0x08) != 0;
    }
    return false;
}

} // namespace srtc

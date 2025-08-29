#include "srtc/packetizer.h"
#include "srtc/packetizer_av1.h"
#include "srtc/packetizer_h264.h"
#include "srtc/packetizer_h265.h"
#include "srtc/packetizer_opus.h"
#include "srtc/packetizer_vp8.h"
#include "srtc/track.h"

namespace srtc
{

Packetizer::Packetizer(const std::shared_ptr<Track>& track)
    : mTrack(track)
{
}

Packetizer::~Packetizer() = default;

void Packetizer::setCodecSpecificData([[maybe_unused]] const std::vector<ByteBuffer>& csd)
{
}

bool Packetizer::isKeyFrame(const ByteBuffer& frame) const
{
    return false;
}

std::pair<std::shared_ptr<Packetizer>, Error> Packetizer::make(const std::shared_ptr<Track>& track)
{
    const auto codec = track->getCodec();
    switch (codec) {
    case Codec::VP8:
        return { std::make_shared<PacketizerVP8>(track), Error::OK };
    case Codec::H264:
        return { std::make_shared<PacketizerH264>(track), Error::OK };
    case Codec::H265:
        return { std::make_shared<PacketizerH265>(track), Error::OK };
    case Codec::AV1:
        return { std::make_shared<PacketizerAV1>(track), Error::OK };
    case Codec::Opus:
        return { std::make_shared<PacketizerOpus>(track), Error::OK };
    default:
        return { nullptr, { Error::Code::InvalidData, "Unsupported codec type" } };
    }
}

std::shared_ptr<Track> Packetizer::getTrack() const
{
    return mTrack;
}

} // namespace srtc

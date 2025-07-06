#include "srtc/depacketizer.h"
#include "srtc/depacketizer_h264.h"
#include "srtc/depacketizer_opus.h"
#include "srtc/track.h"

namespace srtc
{

Depacketizer::Depacketizer(const std::shared_ptr<Track>& track)
	: mTrack(track)
{
}

Depacketizer::~Depacketizer()
{
}

std::pair<std::shared_ptr<Depacketizer>, Error> Depacketizer::make(const std::shared_ptr<Track>& track)
{
	const auto codec = track->getCodec();
	switch (codec) {
	case Codec::H264:
		return { std::make_shared<DepacketizerH264>(track), Error::OK };
	case Codec::Opus:
		return { std::make_shared<DepacketizerOpus>(track), Error::OK };
	default:
		return { nullptr, { Error::Code::InvalidData, "Unsupported codec type" } };
	}
}

std::shared_ptr<Track> Depacketizer::getTrack() const
{
	return mTrack;
}

} // namespace srtc
#include "srtc/packetizer.h"
#include "srtc/packetizer_h264.h"

namespace srtc {

Packetizer::Packetizer() = default;

Packetizer::~Packetizer() = default;

std::pair<std::shared_ptr<Packetizer>, Error> Packetizer::makePacketizer(const Codec& codec)
{
    switch (codec) {
        case Codec::H264:
            return { std::make_shared<PacketizerH264>(), Error::OK };
        default:
            return { nullptr, { Error::Code::InvalidData, "Unsupported codec type" }};
    }
}

}

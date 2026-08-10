#include "srtc/packetizer_audio.h"

namespace srtc
{
PacketizerAudio::PacketizerAudio(const std::shared_ptr<Track>& track)
    : Packetizer(track)
{
}

PacketizerAudio::~PacketizerAudio() = default;

}

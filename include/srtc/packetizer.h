#pragma once

#include "srtc/error.h"
#include "srtc/srtc.h"

#include <utility>

namespace srtc {

class Packetizer {
public:
    Packetizer();
    virtual ~Packetizer();

    static std::pair<std::shared_ptr<Packetizer>, Error> makePacketizer(const Codec& codec);
};

}

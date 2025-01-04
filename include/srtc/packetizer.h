#pragma once

#include "srtc/error.h"
#include "srtc/srtc.h"

#include <utility>

namespace srtc {

class ByteBuffer;

class Packetizer {
public:
    Packetizer();
    virtual ~Packetizer();

    virtual void process(const ByteBuffer& frame) = 0;

    static std::pair<std::shared_ptr<Packetizer>, Error> makePacketizer(const Codec& codec);
};

}

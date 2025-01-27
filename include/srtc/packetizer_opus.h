#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/packetizer.h"

namespace srtc {

class PacketizerOpus final : public Packetizer {
public:
    PacketizerOpus();
    ~PacketizerOpus() override;

    std::list<std::shared_ptr<RtpPacket>> generate(const std::shared_ptr<Track>& track,
                                                   const ByteBuffer& frame) override;
};

}

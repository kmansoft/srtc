#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/packetizer.h"

namespace srtc {

class PacketizerH264 final : public Packetizer {
public:
    PacketizerH264();
    ~PacketizerH264() override;

    void setCodecSpecificData(const std::vector<ByteBuffer>& csd) override;
    std::list<RtpPacket> generate(uint8_t payloadType,
                                  uint32_t ssrc,
                                  const ByteBuffer& frame) override;

private:
    std::vector<ByteBuffer> mCSD;       // Without NALU header
};

}

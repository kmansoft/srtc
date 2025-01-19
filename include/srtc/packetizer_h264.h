#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/packetizer.h"

namespace srtc {

class PacketizerH264 final : public Packetizer {
public:
    PacketizerH264();
    ~PacketizerH264() override;

    void setCodecSpecificData(const std::vector<ByteBuffer>& csd) override;
    std::list<std::shared_ptr<RtpPacket>> generate(const std::shared_ptr<Track>& track,
                                                   const ByteBuffer& frame) override;

private:
    std::vector<ByteBuffer> mCSD;       // Without NALU header
};

}

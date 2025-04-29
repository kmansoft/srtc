#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/packetizer.h"

namespace srtc {

class PacketizerH264 final : public Packetizer {
public:
    PacketizerH264(const std::shared_ptr<Track>& track);
    ~PacketizerH264() override;

    void setCodecSpecificData(const std::vector<ByteBuffer>& csd) override;
    bool isKeyFrame(const ByteBuffer& frame) const override;
    std::list<std::shared_ptr<RtpPacket>> generate(const RtpExtension& extension,
                                                   bool addExtensionToAllPackets,
                                                   size_t mediaProtectionOverhead,
                                                   const ByteBuffer& frame) override;

private:
    std::vector<ByteBuffer> mCSD;       // Without NALU header
};

}

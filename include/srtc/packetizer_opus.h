#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/packetizer.h"

namespace srtc
{

class PacketizerOpus final : public Packetizer
{
public:
    explicit PacketizerOpus(const std::shared_ptr<Track>& track);
    ~PacketizerOpus() override;

    std::list<std::shared_ptr<RtpPacket>> generate(const std::shared_ptr<RtpExtensionSource>& simulcast,
                                                   const std::shared_ptr<RtpExtensionSource>& twcc,
                                                   size_t mediaProtectionOverhead,
                                                   const ByteBuffer& frame) override;
};

} // namespace srtc

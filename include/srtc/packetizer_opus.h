#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/packetizer_audio.h"

namespace srtc
{

class PacketizerOpus final : public PacketizerAudio
{
public:
    explicit PacketizerOpus(const std::shared_ptr<Track>& track);
    ~PacketizerOpus() override;

    [[nodiscard]] std::vector<std::shared_ptr<RtpPacket>> generate(
        const std::vector<std::shared_ptr<RtpExtensionSource>>& extensionSourceList,
        size_t mediaProtectionOverhead,
        int64_t pts_usec,
        const ByteBuffer& frame) override;
};

} // namespace srtc

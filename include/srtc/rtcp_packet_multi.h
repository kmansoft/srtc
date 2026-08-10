#pragma once

#include "srtc/byte_buffer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace srtc
{

class RtcpPacket;
class ByteBuffer;

class RtcpPacketMulti
{
public:
    RtcpPacketMulti(const std::vector<std::shared_ptr<RtcpPacket>>& packetList);
    ~RtcpPacketMulti();

    [[nodiscard]] ByteBuffer generate() const;

private:
    const ByteBuffer mMulti;
};

} // namespace srtc

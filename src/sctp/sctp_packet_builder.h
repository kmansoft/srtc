#pragma once

#include "srtc/byte_buffer.h"

#include <cstdint>

namespace srtc::sctp {

class SctpPacketBuilder
{
public:
    SctpPacketBuilder(uint16_t srcPort, uint16_t dstPort, uint32_t verificationTag);
    ~SctpPacketBuilder();

    void addChunk(uint8_t type, uint8_t flags, const uint8_t* data, size_t length);
    void addChunk(uint8_t type, uint8_t flags, const ByteBuffer& buf);

    [[nodiscard]] ByteBuffer build();

private:
    ByteBuffer mBuf;
    ByteWriter mWriter;
};

} // namespace srtc::sctp

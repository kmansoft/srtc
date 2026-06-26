#include "sctp_packet_builder.h"
#include "sctp_crc32.h"

#include <utility>

namespace srtc::sctp {

SctpPacketBuilder::SctpPacketBuilder(uint16_t srcPort, uint16_t dstPort, uint32_t verificationTag)
    : mWriter(mBuf)
{
    mWriter.writeU16(srcPort);
    mWriter.writeU16(dstPort);
    mWriter.writeU32(verificationTag);
    mWriter.writeU32(0); // CRC-32c placeholder at offset 8
}

SctpPacketBuilder::~SctpPacketBuilder() = default;

void SctpPacketBuilder::addChunk(uint8_t type, uint8_t flags, const uint8_t* data, size_t length)
{
    const auto chunkLen = static_cast<uint16_t>(4 + length);
    mWriter.writeU8(type);
    mWriter.writeU8(flags);
    mWriter.writeU16(chunkLen);
    if (length > 0) {
        mWriter.write(data, length);
    }

    // Pad to 4-byte boundary (padding bytes are not counted in chunkLen)
    const size_t padded = (chunkLen + 3) & ~3u;
    if (padded > chunkLen) {
        mWriter.padding(0, padded - chunkLen);
    }
}

void SctpPacketBuilder::addChunk(uint8_t type, uint8_t flags, const ByteBuffer& buf)
{
    addChunk(type, flags, buf.data(), buf.size());
}

ByteBuffer SctpPacketBuilder::build()
{
    uint8_t* data = mBuf.data();
    const size_t size = mBuf.size();

    // Patch CRC-32c at offset 8 as raw bytes in network byte order.
    // crc32c() on little-endian returns an integer whose memory layout already
    // equals the correct network-byte-order bytes, so write LSB-first.
    const uint32_t crc = crc32c(data, size);
    data[8]  = static_cast<uint8_t>(crc);
    data[9]  = static_cast<uint8_t>(crc >> 8);
    data[10] = static_cast<uint8_t>(crc >> 16);
    data[11] = static_cast<uint8_t>(crc >> 24);

    return std::move(mBuf);
}

} // namespace srtc::sctp

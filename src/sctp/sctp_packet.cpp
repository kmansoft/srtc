#include "sctp/sctp_packet.h"
#include "sctp/sctp_crc32.h"
#include "srtc/byte_buffer.h"

#include <algorithm>

namespace srtc::sctp {

std::optional<SctpPacket> SctpPacket::parse(const uint8_t* data, size_t size)
{
    if (size < 12) {
        return std::nullopt;
    }

    // Validate CRC-32c: compute over packet with bytes 8-11 treated as zero
    const uint32_t storedCrc = static_cast<uint32_t>(data[8])
                             | static_cast<uint32_t>(data[9])  << 8
                             | static_cast<uint32_t>(data[10]) << 16
                             | static_cast<uint32_t>(data[11]) << 24;

    const uint8_t zeros[4] = {};
    uint32_t crc = crc32c_update(0xFFFFFFFFu, data, 8);
    crc = crc32c_update(crc, zeros, 4);
    crc = crc32c_update(crc, data + 12, size - 12);

    uint32_t finalized = crc32c_finalize(crc);
    if (finalized != storedCrc) {
        return std::nullopt;
    }

    ByteReader r(data, size);
    SctpPacket packet;
    packet.mSrcPort         = r.readU16();
    packet.mDstPort         = r.readU16();
    packet.mVerificationTag = r.readU32();
    r.skip(4); // skip CRC field

    while (r.remaining() >= 4) {
        const auto chunkType  = r.readU8();
        const auto chunkFlags = r.readU8();
        const auto chunkLen   = r.readU16();

        if (chunkLen < 4 || chunkLen - 4u > r.remaining()) {
            return std::nullopt;
        }

        packet.mChunks.push_back({
            chunkType,
            chunkFlags,
            data + r.position(),
            static_cast<size_t>(chunkLen - 4)
        });

        const size_t paddedSkip = ((size_t)(chunkLen + 3) & ~3u) - 4;
        r.skip(std::min(paddedSkip, r.remaining()));
    }

    return packet;
}

std::vector<SctpPacket::Param> SctpPacket::Chunk::parseParams(size_t offset) const
{
    std::vector<Param> params;

    if (offset > size) return params;

    ByteReader r(data, size);
    r.skip(offset);

    while (r.remaining() >= 4) {
        const auto paramType = r.readU16();
        const auto paramLen  = r.readU16();

        if (paramLen < 4 || paramLen - 4u > r.remaining()) {
            break;
        }

        params.push_back({
            paramType,
            data + r.position(),
            static_cast<size_t>(paramLen - 4)
        });

        const size_t paddedSkip = ((size_t)(paramLen + 3) & ~3u) - 4;
        r.skip(std::min(paddedSkip, r.remaining()));
    }

    return params;
}

} // namespace srtc::sctp

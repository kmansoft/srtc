#include "sctp/sctp_packet.h"
#include "sctp/sctp_crc32.h"

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
    if (crc32c_finalize(crc) != storedCrc) {
        return std::nullopt;
    }

    SctpPacket packet;
    packet.mSrcPort         = static_cast<uint16_t>(data[0] << 8 | data[1]);
    packet.mDstPort         = static_cast<uint16_t>(data[2] << 8 | data[3]);
    packet.mVerificationTag = static_cast<uint32_t>(data[4]) << 24
                            | static_cast<uint32_t>(data[5]) << 16
                            | static_cast<uint32_t>(data[6]) << 8
                            | static_cast<uint32_t>(data[7]);

    // Parse chunks
    size_t pos = 12;
    while (pos + 4 <= size) {
        const uint8_t  chunkType  = data[pos];
        const uint8_t  chunkFlags = data[pos + 1];
        const uint16_t chunkLen   = static_cast<uint16_t>(data[pos + 2] << 8 | data[pos + 3]);

        if (chunkLen < 4 || pos + chunkLen > size) {
            return std::nullopt;
        }

        packet.mChunks.push_back({
            chunkType,
            chunkFlags,
            data + pos + 4,
            static_cast<size_t>(chunkLen - 4)
        });

        // Advance past chunk and its padding
        const size_t padded = (chunkLen + 3) & ~3u;
        pos += padded;
    }

    return packet;
}

std::vector<SctpPacket::Param> SctpPacket::Chunk::parseParams(size_t offset) const
{
    std::vector<Param> params;

    while (offset + 4 <= size) {
        const auto paramType = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        const auto paramLen  = static_cast<uint16_t>(data[offset + 2] << 8 | data[offset + 3]);

        if (paramLen < 4 || offset + paramLen > size) {
            break;
        }

        params.push_back({
            paramType,
            data + offset + 4,
            static_cast<size_t>(paramLen - 4)
        });

        const size_t padded = (paramLen + 3) & ~3u;
        offset += padded;
    }

    return params;
}

} // namespace srtc::sctp

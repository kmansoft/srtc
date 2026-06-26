#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace srtc::sctp {

class SctpPacket
{
public:
    struct Param {
        uint16_t type;
        const uint8_t* data;  // points into original buffer, not including param header
        size_t size;
    };

    struct Chunk {
        uint8_t type;
        uint8_t flags;
        const uint8_t* data;  // points into original buffer, not including chunk header
        size_t size;

        // Parse optional parameters starting at a given offset within data.
        // Use offset=16 for INIT / INIT ACK (skips the fixed fields).
        [[nodiscard]] std::vector<Param> parseParams(size_t offset = 0) const;
    };

    // Parse and CRC-validate an SCTP packet. Returns nullopt if malformed or CRC mismatch.
    [[nodiscard]] static std::optional<SctpPacket> parse(const uint8_t* data, size_t size);

    [[nodiscard]] uint16_t srcPort() const { return mSrcPort; }
    [[nodiscard]] uint16_t dstPort() const { return mDstPort; }
    [[nodiscard]] uint32_t verificationTag() const { return mVerificationTag; }
    [[nodiscard]] const std::vector<Chunk>& chunks() const { return mChunks; }

private:
    SctpPacket() = default;

    uint16_t mSrcPort = 0;
    uint16_t mDstPort = 0;
    uint32_t mVerificationTag = 0;
    std::vector<Chunk> mChunks;
};

} // namespace srtc::sctp

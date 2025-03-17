#pragma once

#include <memory>
#include <cstdint>

namespace srtc {

class RtpPacketSource {
public:
    RtpPacketSource(uint32_t ssrc,
                    uint8_t payloadId);
    ~RtpPacketSource() = default;

    [[nodiscard]] uint32_t getSSRC() const;
    [[nodiscard]] uint8_t getPayloadId() const;

    [[nodiscard]] std::pair<uint32_t, uint16_t> getNextSequence();

private:
    const uint32_t mSSRC;
    const uint8_t mPayloadId;
    uint32_t mGeneratedCount;
    uint32_t mRollover;
    uint16_t mNextSequence;
};

}

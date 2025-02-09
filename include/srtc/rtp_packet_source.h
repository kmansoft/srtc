#pragma once

#include <cstdint>

namespace srtc {

class RtpPacketSource {
public:
    RtpPacketSource(uint32_t ssrc,
                    uint8_t payloadId);
    ~RtpPacketSource() = default;

    [[nodiscard]] uint32_t getSSRC() const;
    [[nodiscard]] uint8_t getPayloadId() const;

    [[nodiscard]] uint32_t getUniqueId() const;
    [[nodiscard]] uint16_t getNextSequence();

private:
    const uint32_t mSSRC;
    const uint8_t mPayloadId;
    const uint32_t mUniqueId;
    uint16_t mNextSequence;
};

}

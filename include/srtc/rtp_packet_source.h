#pragma once

#include <cstdint>

namespace srtc {

class RtpPacketSource {
public:
    RtpPacketSource();
    ~RtpPacketSource() = default;

    [[nodiscard]] uint32_t getUniqueId() const;
    [[nodiscard]] uint16_t getNextSequence();

private:
    uint32_t mUniqueId;
    uint16_t mNextSequence;
};

}

#pragma once

#include <cstdint>

namespace srtc
{

class RtcpPacketSource
{
public:
    RtcpPacketSource(uint32_t ssrc);
    ~RtcpPacketSource();

    [[nodiscard]] uint32_t getSSRC() const;
    [[nodiscard]] uint32_t getNextSequence();

    void clear();

private:
    const uint32_t mSSRC;
    uint32_t mNextSequence;
};

} // namespace srtc
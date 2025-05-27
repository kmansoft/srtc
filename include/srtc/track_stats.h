#pragma once

#include <atomic>

namespace srtc
{

class TrackStats
{
public:
    TrackStats();
    ~TrackStats();

    void clear();

    [[nodiscard]] uint32_t getSentPackets() const;
    [[nodiscard]] uint32_t getSentBytes() const;

    void incrementSentPackets(uint32_t increment);
    void incrementSentBytes(uint32_t increment);

private:
    uint32_t mSentPackets;
    uint32_t mSentBytes;
};

} // namespace srtc
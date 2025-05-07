#pragma once

#include <list>
#include <memory>
#include <unordered_map>

#include <cstdint>

namespace srtc
{

class RtpPacket;

class SendHistory
{
public:
    SendHistory();
    ~SendHistory();

    void save(const std::shared_ptr<RtpPacket>& packet);

    [[nodiscard]] std::shared_ptr<RtpPacket> find(uint32_t ssrc, uint16_t sequence) const;

private:
    struct TrackHistory {
        std::list<std::shared_ptr<RtpPacket>> packetList;
        std::unordered_map<uint32_t, std::shared_ptr<RtpPacket>> packetMap;
    };

    std::unordered_map<uint32_t, TrackHistory> mTrackMap;
};

} // namespace srtc

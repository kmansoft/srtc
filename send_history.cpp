#include "srtc/send_history.h"
#include "srtc/rtp_packet.h"

namespace {

constexpr auto kMaxHistory = 100;

}

namespace srtc {

SendHistory::SendHistory() = default;

SendHistory::~SendHistory() = default;

void SendHistory::save(const std::shared_ptr<RtpPacket>& packet)
{
    const auto ssrc = packet->getSSRC();

    auto& item = mTrackMap[ssrc];
    while (item.packetList.size() >= kMaxHistory) {
        auto& packetList = item.packetList;
        auto& packetMap = item.packetMap;
        if (const auto iter = packetMap.find(packetList.back()->getSequence());
                iter != packetMap.end()) {
            packetMap.erase(iter);
        }
        packetList.pop_back();
    }

    item.packetList.push_front(packet);
    item.packetMap.insert_or_assign(packet->getSequence(), packet);
}

std::shared_ptr<RtpPacket> SendHistory::find(uint32_t ssrc, uint16_t sequence) const
{
    if (const auto i1 = mTrackMap.find(ssrc); i1 != mTrackMap.end()) {
        const auto& packetMap = i1->second.packetMap;
        if (const auto i2 = packetMap.find(sequence); i2 != packetMap.end()) {
            return i2->second;
        }
    }

    return nullptr;
}

}

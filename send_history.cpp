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
        item.packetList.pop_back();
    }

    item.packetList.push_front(packet);
}

std::shared_ptr<RtpPacket> SendHistory::find(uint32_t ssrc, uint16_t sequence) const
{
    const auto iter = mTrackMap.find(ssrc);
    if (iter != mTrackMap.end()) {
        for (const auto& packet : iter->second.packetList) {
            if (packet->getSequence() == sequence) {
                return packet;
            }
        }
    }

    return nullptr;
}

}

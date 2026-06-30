#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace srtc::twcc
{
class SubscribePacketHistory;
}; // namespace srtc::twcc

namespace srtc
{

class SdpOffer;
class SdpAnswer;
class RtpPacket;
class RtcpPacket;
class Track;

class RtpResponderTWCC final
{
public:
    RtpResponderTWCC();
    ~RtpResponderTWCC();

    static std::shared_ptr<RtpResponderTWCC> factory(const std::shared_ptr<SdpOffer>& offer,
                                                     const std::shared_ptr<SdpAnswer>& answer);

    void onMediaPacket(const std::shared_ptr<RtpPacket>& packet);

    [[nodiscard]] std::vector<std::shared_ptr<RtcpPacket>> run(const std::shared_ptr<Track>& track);

private:
    const std::unique_ptr<twcc::SubscribePacketHistory> mPacketHistory;

    [[nodiscard]] uint8_t getExtensionId(const std::shared_ptr<Track>& track) const;
};

} // namespace srtc
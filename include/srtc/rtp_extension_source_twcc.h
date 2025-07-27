#pragma once

#include "srtc/rtp_extension_source.h"
#include "srtc/scheduler.h"
#include "srtc/temp_buffer.h"
#include "srtc/util.h"

#include <cstdint>
#include <list>
#include <memory>

namespace srtc::twcc
{
class PublishPacketHistory;
}; // namespace srtc::twcc

namespace srtc
{

class Track;
class ByteBuffer;
class ByteReader;
class Packetizer;
class RtpExtensionBuilder;
class SdpOffer;
class SdpAnswer;
class RtpPacket;
class RtpPacket;
class RealScheduler;

class RtpExtensionSourceTWCC : public RtpExtensionSource
{
public:
    RtpExtensionSourceTWCC(uint8_t nVideoExtTWCC,
                           uint8_t nAudioExtTWCC,
                           const std::shared_ptr<RealScheduler>& scheduler);
    ~RtpExtensionSourceTWCC() override;

    static std::shared_ptr<RtpExtensionSourceTWCC> factory(const std::shared_ptr<SdpOffer>& offer,
                                                           const std::shared_ptr<SdpAnswer>& answer,
                                                           const std::shared_ptr<RealScheduler>& scheduler);

    void onPeerConnected();

    [[nodiscard]] uint8_t getPadding(const std::shared_ptr<Track>& track, size_t remainingDataSize) override;

    [[nodiscard]] bool wantsExtension(const std::shared_ptr<Track>& track,
                                      bool isKeyFrame,
                                      int packetNumber) const override;

    void addExtension(RtpExtensionBuilder& builder,
                      const std::shared_ptr<Track>& track,
                      bool isKeyFrame,
                      int packetNumber) override;

    void onBeforeGeneratingRtpPacket(const std::shared_ptr<RtpPacket>& packet);
    void onBeforeSendingRtpPacket(const std::shared_ptr<RtpPacket>& packet, size_t generatedSize, size_t encryptedSize);
    void onPacketWasNacked(const std::shared_ptr<RtpPacket>& packet);

    void onReceivedRtcpPacket(uint32_t ssrc, ByteReader& reader);

    [[nodiscard]] std::optional<uint16_t> getFeedbackSeq(const std::shared_ptr<RtpPacket>& packet) const;

    [[nodiscard]] unsigned int getPacingSpreadMillis(const std::list<std::shared_ptr<RtpPacket>>& list,
                                                     float bandwidthScale,
                                                     unsigned int defaultValue) const;
    void updatePublishConnectionStats(PublishConnectionStats& stats) const;

private:
    const uint8_t mVideoExtTWCC;
    const uint8_t mAudioExtTWCC;
    uint16_t mNextPacketSEQ;
    std::unique_ptr<twcc::PublishPacketHistory> mPacketHistory;

    struct TempPacket {
        int32_t delta_micros;
        uint8_t status;
    };
    FixedTempBuffer<TempPacket> mTempPacketBuffer;

    [[nodiscard]] uint8_t getExtensionId(const std::shared_ptr<Track>& track) const;

    // Probing
    bool mIsConnected;
    bool mIsProbing;
    unsigned int mProbingPacketCount;

    std::weak_ptr<Task> mTaskStartProbing;
    std::weak_ptr<Task> mTaskEndProbing;

    void onStartProbing();
    void onEndProbing();

    ScopedScheduler mScheduler;
};

} // namespace srtc

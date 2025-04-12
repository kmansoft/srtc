#pragma once

#include "srtc/srtc.h"
#include "srtc/optional.h"

#include <memory>
#include <string>
#include <atomic>

namespace srtc {

class RtpPacketSource;

class Track {
public:
    struct SimulcastLayer {
        std::string ridName;
        uint16_t ridIndex;  // [0..3]
        uint16_t width;
        uint16_t height;
        uint32_t kilobitPerSecond;
    };

    Track(int trackId,
          MediaType mediaType,
          const std::string& mediaId,
          uint32_t ssrc,
          int payloadId,
          uint32_t rtxSsrc,
          int rtxPayloadId,
          Codec codec,
          const srtc::optional<SimulcastLayer>& simulcastLayer,
          bool hasNack,
          bool hasPli,
          int profileLevelId = 0);

    [[nodiscard]] int getTrackId() const;
    [[nodiscard]] MediaType getMediaType() const;
    [[nodiscard]] std::string getMediaId() const;
    [[nodiscard]] int getPayloadId() const;
    [[nodiscard]] int getRtxPayloadId() const;
    [[nodiscard]] Codec getCodec() const;
    [[nodiscard]] bool isSimulcast() const;
    [[nodiscard]] const SimulcastLayer& getSimulcastLayer() const;
    [[nodiscard]] bool hasNack() const;
    [[nodiscard]] bool hasPli() const;
    [[nodiscard]] int getProfileLevelId() const;

    [[nodiscard]] uint32_t getSSRC() const;
    [[nodiscard]] uint32_t getRtxSSRC() const;

    [[nodiscard]] std::shared_ptr<RtpPacketSource> getPacketSource() const;
    [[nodiscard]] std::shared_ptr<RtpPacketSource> getRtxPacketSource() const;

    [[nodiscard]] size_t getSentPacketCount() const;
    void incrementSentPacketCount(size_t increment);

private:
    const int mTrackId;
    const MediaType mMediaType;
    const std::string mMediaId;
    const uint32_t mSSRC;
    const int mPayloadId;
    const uint32_t mRtxSSRC;
    const int mRtxPayloadId;
    const Codec mCodec;
    const srtc::optional<SimulcastLayer> mSimulcastLayer;
    const bool mHasNack;
    const bool mHasPli;
    const int mProfileLevelId;
    const std::shared_ptr<RtpPacketSource> mPacketSource;
    const std::shared_ptr<RtpPacketSource> mRtxPacketSource;
    std::atomic<size_t> mSentPacketCount;
};

}

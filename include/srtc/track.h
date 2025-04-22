#pragma once

#include "srtc/srtc.h"
#include "srtc/optional.h"
#include "srtc/simulcast_layer.h"

#include <memory>
#include <string>
#include <atomic>

namespace srtc {

class RtcpPacketSource;
class RtpTimeSource;
class RtpPacketSource;
class TrackStats;

class Track {
public:
    struct SimulcastLayer : public srtc::SimulcastLayer {
        uint16_t index;  // [0..3]
    };

    Track(int trackId,
          MediaType mediaType,
          const std::string& mediaId,
          uint32_t ssrc,
          int payloadId,
          uint32_t rtxSsrc,
          int rtxPayloadId,
          Codec codec,
          uint32_t clockRate,
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
    [[nodiscard]] uint32_t getClockRate() const;
    [[nodiscard]] bool isSimulcast() const;
    [[nodiscard]] const SimulcastLayer& getSimulcastLayer() const;
    [[nodiscard]] bool hasNack() const;
    [[nodiscard]] bool hasPli() const;
    [[nodiscard]] int getProfileLevelId() const;

    [[nodiscard]] uint32_t getSSRC() const;
    [[nodiscard]] uint32_t getRtxSSRC() const;

    [[nodiscard]] std::shared_ptr<RtcpPacketSource> getRtcpPacketSource() const;
    [[nodiscard]] std::shared_ptr<RtpTimeSource> getRtpTimeSource() const;
    [[nodiscard]] std::shared_ptr<RtpPacketSource> getRtpPacketSource() const;
    [[nodiscard]] std::shared_ptr<RtpPacketSource> getRtxPacketSource() const;

    [[nodiscard]] std::shared_ptr<TrackStats> getStats() const;

private:
    const int mTrackId;
    const MediaType mMediaType;
    const std::string mMediaId;
    const uint32_t mSSRC;
    const int mPayloadId;
    const uint32_t mRtxSSRC;
    const int mRtxPayloadId;
    const Codec mCodec;
    const uint32_t mClockRate;
    const srtc::optional<SimulcastLayer> mSimulcastLayer;
    const bool mHasNack;
    const bool mHasPli;
    const int mProfileLevelId;
    const std::shared_ptr<RtcpPacketSource> mRtcpPacketSource;
    const std::shared_ptr<RtpTimeSource> mRtpTimeSource;
    const std::shared_ptr<RtpPacketSource> mRtpPacketSource;
    const std::shared_ptr<RtpPacketSource> mRtxPacketSource;
    const std::shared_ptr<TrackStats> mStats;
};

}

#pragma once

#include "srtc/simulcast_layer.h"
#include "srtc/sender_report.h"
#include "srtc/srtc.h"

#include <atomic>
#include <memory>
#include <string>

namespace srtc
{

class RtcpPacketSource;
class RtpTimeSource;
class RtpPacketSource;
class TrackStats;

class Track
{
public:
    struct SimulcastLayer : public srtc::SimulcastLayer {
        uint16_t index = { 0 }; // [0..3]
    };

    struct CodecOptions {
        // Video
        const int profileLevelId;
        // Audio
        const int minptime;
        const bool stereo;

        CodecOptions(int profileLevelId, int minptime, bool stereo)
            : profileLevelId(profileLevelId)
            , minptime(minptime)
            , stereo(stereo)
        {
        }
    };

    Track(uint32_t trackId,
		  Direction direction,
          MediaType mediaType,
          const std::string& mediaId,
          uint32_t ssrc,
		  uint8_t payloadId,
          uint32_t rtxSsrc,
		  uint8_t rtxPayloadId,
          Codec codec,
          const std::shared_ptr<CodecOptions>& codecOptions,
          const std::shared_ptr<SimulcastLayer>& simulcastLayer,
          uint32_t clockRate,
          bool hasNack,
          bool hasPli);

    [[nodiscard]] uint32_t getTrackId() const;
	[[nodiscard]] Direction getDirection() const;
    [[nodiscard]] MediaType getMediaType() const;
    [[nodiscard]] std::string getMediaId() const;
    [[nodiscard]] uint8_t getPayloadId() const;
    [[nodiscard]] uint8_t getRtxPayloadId() const;
    [[nodiscard]] Codec getCodec() const;
    [[nodiscard]] std::shared_ptr<CodecOptions> getCodecOptions() const;
    [[nodiscard]] bool isSimulcast() const;
    [[nodiscard]] std::shared_ptr<SimulcastLayer> getSimulcastLayer() const;
    [[nodiscard]] uint32_t getClockRate() const;
    [[nodiscard]] bool hasNack() const;
    [[nodiscard]] bool hasPli() const;

    [[nodiscard]] uint32_t getSSRC() const;
    [[nodiscard]] uint32_t getRtxSSRC() const;

    [[nodiscard]] std::shared_ptr<RtcpPacketSource> getRtcpPacketSource() const;
    [[nodiscard]] std::shared_ptr<RtpTimeSource> getRtpTimeSource() const;
    [[nodiscard]] std::shared_ptr<RtpPacketSource> getRtpPacketSource() const;
    [[nodiscard]] std::shared_ptr<RtpPacketSource> getRtxPacketSource() const;

    [[nodiscard]] std::shared_ptr<TrackStats> getStats() const;

private:
    const uint32_t mTrackId;
	const Direction mDirection;
    const MediaType mMediaType;
    const std::string mMediaId;
    const uint32_t mSSRC;
    const uint8_t mPayloadId;
    const uint32_t mRtxSSRC;
    const uint8_t mRtxPayloadId;
    const Codec mCodec;
    const std::shared_ptr<CodecOptions> mCodecOptions;
    const std::shared_ptr<SimulcastLayer> mSimulcastLayer;
    const uint32_t mClockRate;
    const bool mHasNack;
    const bool mHasPli;
    const std::shared_ptr<RtcpPacketSource> mRtcpPacketSource;
    const std::shared_ptr<RtpTimeSource> mRtpTimeSource;
    const std::shared_ptr<RtpPacketSource> mRtpPacketSource;
    const std::shared_ptr<RtpPacketSource> mRtxPacketSource;
    const std::shared_ptr<TrackStats> mStats;
};

} // namespace srtc

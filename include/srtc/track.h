#pragma once

#include "srtc/srtc.h"

#include <memory>

namespace srtc {

class RtpPacketSource;

class Track {
public:
    Track(int trackId,
          MediaType mediaType,
          uint32_t ssrc,
          int payloadId,
          uint32_t rtxSsrc,
          int rtxPayloadId,
          Codec codec,
          bool hasNack,
          bool hasPli,
          int profileLevelId = 0);

    [[nodiscard]] int getTrackId() const;
    [[nodiscard]] MediaType getMediaType() const;
    [[nodiscard]] int getPayloadId() const;
    [[nodiscard]] int getRtxPayloadId() const;
    [[nodiscard]] Codec getCodec() const;
    [[nodiscard]] bool hasNack() const;
    [[nodiscard]] bool hasPli() const;
    [[nodiscard]] int getProfileLevelId() const;

    [[nodiscard]] uint32_t getSSRC() const;
    [[nodiscard]] uint32_t getRtxSSRC() const;

    [[nodiscard]] std::shared_ptr<RtpPacketSource> getPacketSource() const;
    [[nodiscard]] std::shared_ptr<RtpPacketSource> getRtxPacketSource() const;

private:
    const int mTrackId;
    const MediaType mMediaType;
    const uint32_t mSSRC;
    const int mPayloadId;
    const uint32_t mRtxSSRC;
    const int mRtxPayloadId;
    const Codec mCodec;
    const bool mHasNack;
    const bool mHasPli;
    const int mProfileLevelId;
    const std::shared_ptr<RtpPacketSource> mPacketSource;
    const std::shared_ptr<RtpPacketSource> mRtxPacketSource;
};

}

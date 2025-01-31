#pragma once

#include "srtc/srtc.h"

namespace srtc {

class Track {
public:
    Track(int trackId,
          MediaType mediaType,
          int payloadId,
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

    void setSSRC(uint32_t ssrc, uint32_t rtx);
    [[nodiscard]] uint32_t getSSRC() const;

    [[nodiscard]] uint32_t getRtxSSRC() const;
    [[nodiscard]] uint16_t getRtxNextSequence();

private:
    const int mTrackId;
    const MediaType mMediaType;
    const int mPayloadId;
    const int mRtxPayloadId;
    const Codec mCodec;
    const bool mHasNack;
    const bool mHasPli;
    const int mProfileLevelId;
    uint32_t mSSRC = { 0 };
    uint32_t mRtxSSRC = { 0 };
    uint16_t mRtxNextSequence;
};

}

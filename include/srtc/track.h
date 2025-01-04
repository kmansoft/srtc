#pragma once

#include "srtc/srtc.h"

namespace srtc {

class Track {
public:
    Track(int trackId,
          int payloadType,
          Codec codec);
    Track(int trackId,
          int payloadType,
          Codec codec,
          int profileId,
          int level);

    [[nodiscard]] int getTrackId() const;
    [[nodiscard]] int getPayloadType() const;
    [[nodiscard]] Codec getCodec() const;
    [[nodiscard]] int getProfileId() const;
    [[nodiscard]] int getLevel() const;

    void setSSRC(int32_t ssrc);
    [[nodiscard]] int32_t getSSRC() const;

private:
    const int mTrackId;
    const int mPayloadType;
    const Codec mCodec;
    const int mProfileId;
    const int mLevel;
    int32_t mSSRC = { 0 };
};

}

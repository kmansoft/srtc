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
          int profileLevelId);

    [[nodiscard]] int getTrackId() const;
    [[nodiscard]] int getPayloadType() const;
    [[nodiscard]] Codec getCodec() const;
    [[nodiscard]] int getProfileLevelId() const;

    void setSSRC(uint32_t ssrc);
    [[nodiscard]] uint32_t getSSRC() const;

private:
    const int mTrackId;
    const int mPayloadType;
    const Codec mCodec;
    const int mProfileLevelId;
    uint32_t mSSRC = { 0 };
};

}

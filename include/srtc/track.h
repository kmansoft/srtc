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

    int getTrackId() const;
    int getPayloadType() const;
    Codec getCodec() const;
    int getProfileId() const;
    int getLevel() const;

private:
    const int mTrackId;
    const int mPayloadType;
    const Codec mCodec;
    const int mProfileId;
    const int mLevel;
};

}

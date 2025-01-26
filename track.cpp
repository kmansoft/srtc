#include "srtc/track.h"

namespace srtc {

Track::Track(int trackId,
             int payloadType,
             Codec codec)
  : mTrackId(trackId)
  , mPayloadType(payloadType)
  , mCodec(codec)
  , mProfileLevelId(0)
{
}

Track::Track(int trackId,
             int payloadType,
             Codec codec,
             int profileLevelId)
    : mTrackId(trackId)
    , mPayloadType(payloadType)
    , mCodec(codec)
    , mProfileLevelId(profileLevelId)
{
}

int Track::getTrackId() const
{
    return mTrackId;
}

int Track::getPayloadType() const
{
    return mPayloadType;
}

Codec Track::getCodec() const
{
    return mCodec;
}

int Track::getProfileLevelId() const
{
    return mProfileLevelId;
}

void Track::setSSRC(uint32_t ssrc)
{
    mSSRC = ssrc;
}

uint32_t Track::getSSRC() const
{
    return mSSRC;
}

}

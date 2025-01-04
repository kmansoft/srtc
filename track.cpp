#include "srtc/track.h"

namespace srtc {

Track::Track(int trackId,
             int payloadType,
             Codec codec)
  : mTrackId(trackId)
  , mPayloadType(payloadType)
  , mCodec(codec)
  , mProfileId(0)
  , mLevel(0)
{
}

Track::Track(int trackId,
             int payloadType,
             Codec codec,
             int profileId,
             int level)
    : mTrackId(trackId)
    , mPayloadType(payloadType)
    , mCodec(codec)
    , mProfileId(profileId)
    , mLevel(level)
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

int Track::getProfileId() const
{
    return mProfileId;
}

int Track::getLevel() const
{
    return mLevel;
}

void Track::setSSRC(int32_t ssrc)
{
    mSSRC = ssrc;
}

int32_t Track::getSSRC() const
{
    return mSSRC;
}

}

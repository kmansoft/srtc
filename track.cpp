#include "srtc/track.h"

namespace srtc {

Track::Track(int trackId,
             int payloadType,
             Codec codec,
             bool hasNack,
             bool hasPli,
             int profileLevelId)
    : mTrackId(trackId)
    , mPayloadType(payloadType)
    , mCodec(codec)
    , mHasNack(hasNack)
    , mHasPli(hasPli)
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

bool Track::hasNack() const
{
    return mHasNack;
}

bool Track::hasPli() const
{
    return mHasPli;
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

#include "srtc/track.h"

namespace srtc {

Track::Track(int trackId,
             MediaType mediaType,
             int payloadId,
             int rtxPayloadId,
             Codec codec,
             bool hasNack,
             bool hasPli,
             int profileLevelId)
    : mTrackId(trackId)
    , mMediaType(mediaType)
    , mPayloadId(payloadId)
    , mRtxPayloadId(rtxPayloadId)
    , mCodec(codec)
    , mHasNack(hasNack)
    , mHasPli(hasPli)
    , mProfileLevelId(profileLevelId)
    , mRtxNextSequence(static_cast<uint16_t>(lrand48()))
{
}

int Track::getTrackId() const
{
    return mTrackId;
}

MediaType Track::getMediaType() const
{
    return mMediaType;
}

int Track::getPayloadId() const
{
    return mPayloadId;
}

int Track::getRtxPayloadId() const
{
    return mRtxPayloadId;
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

void Track::setSSRC(uint32_t ssrc, uint32_t rtx)
{
    mSSRC = ssrc;
    mRtxSSRC = rtx;
}

uint32_t Track::getSSRC() const
{
    return mSSRC;
}

uint32_t Track::getRtxSSRC() const
{
    return mRtxSSRC;
}

uint16_t Track::getRtxNextSequence()
{
    return ++mRtxNextSequence;
}

}

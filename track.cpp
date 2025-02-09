#include "srtc/track.h"
#include "srtc/rtp_packet_source.h"

namespace srtc {

Track::Track(int trackId,
             MediaType mediaType,
             uint32_t ssrtc,
             int payloadId,
             uint32_t rtxSsrc,
             int rtxPayloadId,
             Codec codec,
             bool hasNack,
             bool hasPli,
             int profileLevelId)
    : mTrackId(trackId)
    , mMediaType(mediaType)
    , mSSRC(ssrtc)
    , mPayloadId(payloadId)
    , mRtxSSRC(rtxSsrc)
    , mRtxPayloadId(rtxPayloadId)
    , mCodec(codec)
    , mHasNack(hasNack)
    , mHasPli(hasPli)
    , mProfileLevelId(profileLevelId)
    , mPacketSource(std::make_shared<RtpPacketSource>(mSSRC, mPayloadId))
    , mRtxPacketSource(std::make_shared<RtpPacketSource>(mRtxSSRC, mRtxPayloadId))
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

uint32_t Track::getSSRC() const
{
    return mSSRC;
}

uint32_t Track::getRtxSSRC() const
{
    return mRtxSSRC;
}

std::shared_ptr<RtpPacketSource> Track::getPacketSource() const
{
    return mPacketSource;
}

std::shared_ptr<RtpPacketSource> Track::getRtxPacketSource() const
{
    return mRtxPacketSource;
}

}

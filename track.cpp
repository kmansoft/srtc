#include "srtc/track.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/rtcp_packet_source.h"

namespace srtc {

Track::Track(int trackId,
             MediaType mediaType,
             const std::string& mediaId,
             uint32_t ssrtc,
             int payloadId,
             uint32_t rtxSsrc,
             int rtxPayloadId,
             Codec codec,
             uint32_t clockRate,
             const srtc::optional<SimulcastLayer>& simulcastLayer,
             bool hasNack,
             bool hasPli,
             int profileLevelId)
    : mTrackId(trackId)
    , mMediaType(mediaType)
    , mMediaId(mediaId)
    , mSSRC(ssrtc)
    , mPayloadId(payloadId)
    , mRtxSSRC(rtxSsrc)
    , mRtxPayloadId(rtxPayloadId)
    , mCodec(codec)
    , mClockRate(clockRate)
    , mSimulcastLayer(simulcastLayer)
    , mHasNack(hasNack)
    , mHasPli(hasPli)
    , mProfileLevelId(profileLevelId)
    , mRtcpPacketSource(std::make_shared<RtcpPacketSource>(mSSRC))
    , mRtpPacketSource(std::make_shared<RtpPacketSource>(mSSRC, mPayloadId))
    , mRtxPacketSource(std::make_shared<RtpPacketSource>(mRtxSSRC, mRtxPayloadId))
    , mSentPacketCount(0)
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

std::string Track::getMediaId() const
{
    return mMediaId;
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

uint32_t Track::getClockRate() const
{
    return mClockRate;
}

bool Track::isSimulcast() const
{
    return mSimulcastLayer.has_value();
}

const Track::SimulcastLayer& Track::getSimulcastLayer() const
{
    return mSimulcastLayer.value();
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

std::shared_ptr<RtcpPacketSource> Track::getRtcpPacketSource() const
{
    return mRtcpPacketSource;
}

std::shared_ptr<RtpPacketSource> Track::getRtpPacketSource() const
{
    return mRtpPacketSource;
}

std::shared_ptr<RtpPacketSource> Track::getRtxPacketSource() const
{
    return mRtxPacketSource;
}

size_t Track::getSentPacketCount() const
{
    return mSentPacketCount;
}

void Track::incrementSentPacketCount(size_t increment)
{
    mSentPacketCount += increment;
}

}

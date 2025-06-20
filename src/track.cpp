#include "srtc/track.h"
#include "srtc/rtcp_packet_source.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/rtp_time_source.h"
#include "srtc/track_stats.h"

namespace srtc
{

Track::Track(int trackId,
             MediaType mediaType,
             const std::string& mediaId,
             uint32_t ssrtc,
             int payloadId,
             uint32_t rtxSsrc,
             int rtxPayloadId,
             Codec codec,
             const std::shared_ptr<Track::CodecOptions>& codecOptions,
             const std::shared_ptr<SimulcastLayer>& simulcastLayer,
             uint32_t clockRate,
             bool hasNack,
             bool hasPli)
    : mTrackId(trackId)
    , mMediaType(mediaType)
    , mMediaId(mediaId)
    , mSSRC(ssrtc)
    , mPayloadId(payloadId)
    , mRtxSSRC(rtxSsrc)
    , mRtxPayloadId(rtxPayloadId)
    , mCodec(codec)
    , mCodecOptions(codecOptions)
    , mSimulcastLayer(simulcastLayer)
    , mClockRate(clockRate)
    , mHasNack(hasNack)
    , mHasPli(hasPli)
    , mRtcpPacketSource(std::make_shared<RtcpPacketSource>(mSSRC))
    , mRtpTimeSource(std::make_shared<RtpTimeSource>(clockRate))
    , mRtpPacketSource(std::make_shared<RtpPacketSource>(mSSRC, mPayloadId))
    , mRtxPacketSource(std::make_shared<RtpPacketSource>(mRtxSSRC, mRtxPayloadId))
    , mStats(std::make_shared<TrackStats>())
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

std::shared_ptr<Track::CodecOptions> Track::getCodecOptions() const
{
    return mCodecOptions;
}

bool Track::isSimulcast() const
{
    return mSimulcastLayer != nullptr;
}

std::shared_ptr<Track::SimulcastLayer> Track::getSimulcastLayer() const
{
    return mSimulcastLayer;
}

bool Track::hasNack() const
{
    return mHasNack;
}

bool Track::hasPli() const
{
    return mHasPli;
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

std::shared_ptr<RtpTimeSource> Track::getRtpTimeSource() const
{
    return mRtpTimeSource;
}

std::shared_ptr<RtpPacketSource> Track::getRtxPacketSource() const
{
    return mRtxPacketSource;
}

std::shared_ptr<TrackStats> Track::getStats() const
{
    return mStats;
}

} // namespace srtc

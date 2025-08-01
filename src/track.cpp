#include "srtc/track.h"
#include "srtc/rtcp_packet_source.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/rtp_time_source.h"
#include "srtc/track_stats.h"

namespace srtc
{

Track::Track(uint32_t trackId,
			 Direction direction,
             MediaType mediaType,
             const std::string& mediaId,
             uint32_t ssrtc,
			 uint8_t payloadId,
             uint32_t rtxSsrc,
			 uint8_t rtxPayloadId,
             uint32_t remoteSsrc,
             Codec codec,
             const std::shared_ptr<Track::CodecOptions>& codecOptions,
             const std::shared_ptr<SimulcastLayer>& simulcastLayer,
             uint32_t clockRate,
             bool hasNack,
             bool hasPli)
    : mTrackId(trackId)
	, mDirection(direction)
    , mMediaType(mediaType)
    , mMediaId(mediaId)
    , mSSRC(ssrtc)
    , mPayloadId(payloadId)
    , mRtxSSRC(rtxSsrc)
    , mRtxPayloadId(rtxPayloadId)
    , mRemoteSSRC(remoteSsrc)
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

uint32_t Track::getTrackId() const
{
    return mTrackId;
}

Direction Track::getDirection() const
{
	return mDirection;
}

MediaType Track::getMediaType() const
{
    return mMediaType;
}

std::string Track::getMediaId() const
{
    return mMediaId;
}

uint8_t Track::getPayloadId() const
{
    return mPayloadId;
}

uint8_t Track::getRtxPayloadId() const
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

uint32_t Track::getRemoteSSRC() const
{
    return mRemoteSSRC;
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

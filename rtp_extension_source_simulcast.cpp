#include "srtc/rtp_extension_source_simulcast.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/track.h"
#include "srtc/track_stats.h"
#include "srtc/packetizer.h"
#include "srtc/logging.h"

#define LOG(level, ...) srtc::log(level, "Simulcast", __VA_ARGS__)

namespace srtc {

RtpExtensionSourceSimulcast::RtpExtensionSourceSimulcast(
        uint8_t nVideoExtMediaId,
        uint8_t nVideoExtStreamId,
        uint8_t nVideoExtRepairedStreamId,
        uint8_t nVideoExtGoogleVLA)
    : mVideoExtMediaId(nVideoExtMediaId)
    , mVideoExtStreamId(nVideoExtStreamId)
    , mVideoExtRepairedStreamId(nVideoExtRepairedStreamId)
    , mVideoExtGoogleVLA(nVideoExtGoogleVLA)
    , mIsExtensionsValid(mVideoExtMediaId > 0 && mVideoExtStreamId > 0 && mVideoExtGoogleVLA > 0)
{
}

RtpExtensionSourceSimulcast::~RtpExtensionSourceSimulcast() = default;

bool RtpExtensionSourceSimulcast::shouldAdd(
        const std::shared_ptr<Track>& track,
        const std::shared_ptr<Packetizer>& packetizer,
        const ByteBuffer& frame)
{
    if (mIsExtensionsValid  && track->isSimulcast()) {
        const auto stats = track->getStats();
        return stats->getSentBytes() < 100 || packetizer->isKeyFrame(frame);
    }
    return false;
}

void RtpExtensionSourceSimulcast::prepare(
    const std::shared_ptr<Track>& track,
    const std::vector<std::shared_ptr<SimulcastLayer>>& layerList)
{
    const auto layer = track->getSimulcastLayer();

    mCurMediaId = track->getMediaId();
    mCurLayerName = layer->name;
    buildGoogleVLA(mCurGoogleVLA, layer->index, layerList);
}

void RtpExtensionSourceSimulcast::clear()
{
    mCurMediaId.clear();
    mCurLayerName.clear();
    mCurGoogleVLA.resize(0);
}

bool RtpExtensionSourceSimulcast::wants(
    const std::shared_ptr<Track>& track,
    bool isKeyFrame,
    int packetNumber)
{
    return !mCurGoogleVLA.empty();
}

void RtpExtensionSourceSimulcast::add(
    RtpExtensionBuilder& builder,
    const std::shared_ptr<Track>& track,
    bool isKeyFrame,
    int packetNumber)
{
    // LOG(SRTC_LOG_V, "Adding extensions for layer %s", mCurLayerName.c_str());

    builder.addStringValue(mVideoExtMediaId, mCurMediaId);
    builder.addStringValue(mVideoExtStreamId, mCurLayerName);
    builder.addBinaryValue(mVideoExtGoogleVLA, mCurGoogleVLA);
}

void RtpExtensionSourceSimulcast::updateForRtx(
    RtpExtensionBuilder& builder,
    const std::shared_ptr<Track>& track) const
{
    const auto layer = track->getSimulcastLayer();

    if (const auto id = mVideoExtMediaId; id != 0) {
        if (!builder.contains(id)) {
            builder.addStringValue(id, track->getMediaId());
        }
    }
    if (const auto id = mVideoExtRepairedStreamId; id != 0) {
        builder.addStringValue(id, layer->name);
    }
}

}
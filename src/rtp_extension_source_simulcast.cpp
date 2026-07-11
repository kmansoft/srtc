#include "srtc/rtp_extension_source_simulcast.h"
#include "srtc/logging.h"
#include "srtc/media.h"
#include "srtc/packetizer.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_std_extensions.h"
#include "srtc/track.h"
#include "srtc/track_stats.h"

#define LOG(level, ...) srtc::log(level, "Simulcast", __VA_ARGS__)

namespace srtc
{

RtpExtensionSourceSimulcast::RtpExtensionSourceSimulcast()
    : mCurExtMediaId(0)
    , mCurExtStreamId(0)
    , mCurExtGoogleVLA(0)
{
}

RtpExtensionSourceSimulcast::~RtpExtensionSourceSimulcast() = default;

std::shared_ptr<RtpExtensionSourceSimulcast> RtpExtensionSourceSimulcast::factory(bool isVideoSimulcast)
{
    if (isVideoSimulcast) {
        return std::make_shared<RtpExtensionSourceSimulcast>();
    }

    return {};
}

bool RtpExtensionSourceSimulcast::shouldAdd(const std::shared_ptr<Track>& track,
                                            const std::shared_ptr<Packetizer>& packetizer,
                                            const ByteBuffer& frame)
{
    if (track->isSimulcast()) {
        const auto media = track->getMedia();
        const auto& extensionMap = media->getExtensionMap();
        mCurExtMediaId = extensionMap.findByName(RtpStandardExtensions::kExtSdesMid);
        mCurExtStreamId = extensionMap.findByName(RtpStandardExtensions::kExtSdesRtpStreamId);
        mCurExtGoogleVLA = extensionMap.findByName(RtpStandardExtensions::kExtGoogleVLA);

        if (mCurExtMediaId > 0 && mCurExtStreamId > 0 && mCurExtGoogleVLA > 0) {
            const auto stats = track->getStats();
            return stats->getSentPackets() < 100 || packetizer->isKeyFrame(frame);
        }
    }
    return false;
}

void RtpExtensionSourceSimulcast::prepare(const std::shared_ptr<Track>& track,
                                          const std::vector<SimulcastLayer>& layerList)
{
    const auto layer = track->getSimulcastLayer();

    mCurMediaId = track->getMedia()->getId();
    mCurLayerName = layer->name;

    buildGoogleVLA(mCurGoogleVLA, layer->index, layerList);
}

void RtpExtensionSourceSimulcast::clear()
{
    mCurMediaId.clear();
    mCurLayerName.clear();
    mCurGoogleVLA.resize(0);
}

uint8_t RtpExtensionSourceSimulcast::getPadding([[maybe_unused]] const std::shared_ptr<Track>& track,
                                                [[maybe_unused]] size_t remainingDataSize)
{
    return 0;
}

bool RtpExtensionSourceSimulcast::wantsExtension([[maybe_unused]] const std::shared_ptr<Track>& track,
                                                 [[maybe_unused]] bool isKeyFrame,
                                                 [[maybe_unused]] unsigned int packetNumber) const
{
    return !mCurGoogleVLA.empty();
}

void RtpExtensionSourceSimulcast::addExtension(RtpExtensionBuilder& builder,
                                               [[maybe_unused]] const std::shared_ptr<Track>& track,
                                               [[maybe_unused]] bool isKeyFrame,
                                               [[maybe_unused]] unsigned int packetNumber)
{
    builder.addStringValue(mCurExtMediaId, mCurMediaId);
    builder.addStringValue(mCurExtStreamId, mCurLayerName);
    builder.addBinaryValue(mCurExtGoogleVLA, mCurGoogleVLA);
}

void RtpExtensionSourceSimulcast::updateForRtx(RtpExtensionBuilder& builder, const std::shared_ptr<Track>& track) const
{
    const auto media = track->getMedia();
    const auto& extensionMap = media->getExtensionMap();

    const auto extMediaId = extensionMap.findByName(RtpStandardExtensions::kExtSdesMid);
    const auto extRepairedStreamId = extensionMap.findByName(RtpStandardExtensions::kExtSdesRtpRepairedStreamId);

    const auto layer = track->getSimulcastLayer();

    if (const auto id = extMediaId; id != 0) {
        if (!builder.contains(id)) {
            builder.addStringValue(id, track->getMedia()->getId());
        }
    }
    if (const auto id = extRepairedStreamId; id != 0) {
        if (!builder.contains(id)) {
            builder.addStringValue(id, layer->name);
        }
    }
}

} // namespace srtc

#include "srtc/rtp_extension_source_simulcast.h"
#include "srtc/logging.h"
#include "srtc/packetizer.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/track.h"
#include "srtc/track_stats.h"

#define LOG(level, ...) srtc::log(level, "Simulcast", __VA_ARGS__)

namespace srtc
{

RtpExtensionSourceSimulcast::RtpExtensionSourceSimulcast(uint8_t nVideoExtMediaId,
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

std::shared_ptr<RtpExtensionSourceSimulcast> RtpExtensionSourceSimulcast::factory(bool isVideoSimulcast,
																				  uint8_t nVideoExtMediaId,
																				  uint8_t nVideoExtStreamId,
																				  uint8_t nVideoExtRepairedStreamId,
																				  uint8_t nVideoExtGoogleVLA)
{
	if (isVideoSimulcast) {
		if (nVideoExtMediaId > 0 && nVideoExtStreamId > 0 && nVideoExtGoogleVLA > 0) {
			return std::make_shared<RtpExtensionSourceSimulcast>(
				nVideoExtMediaId, nVideoExtStreamId, nVideoExtRepairedStreamId, nVideoExtGoogleVLA);
		}
	}

	return {};
}

bool RtpExtensionSourceSimulcast::shouldAdd(const std::shared_ptr<Track>& track,
											const std::shared_ptr<Packetizer>& packetizer,
											const ByteBuffer& frame)
{
	if (mIsExtensionsValid && track->isSimulcast()) {
		const auto stats = track->getStats();
		return stats->getSentPackets() < 100 || packetizer->isKeyFrame(frame);
	}
	return false;
}

void RtpExtensionSourceSimulcast::prepare(const std::shared_ptr<Track>& track,
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

uint8_t RtpExtensionSourceSimulcast::getPadding([[maybe_unused]] const std::shared_ptr<Track>& track,
												[[maybe_unused]] size_t remainingDataSize)
{
	return 0;
}

bool RtpExtensionSourceSimulcast::wantsExtension(const std::shared_ptr<Track>& track,
												 bool isKeyFrame,
												 unsigned int packetNumber) const
{
	return !mCurGoogleVLA.empty();
}

void RtpExtensionSourceSimulcast::addExtension(RtpExtensionBuilder& builder,
											   const std::shared_ptr<Track>& track,
											   bool isKeyFrame,
											   unsigned int packetNumber)
{
	builder.addStringValue(mVideoExtMediaId, mCurMediaId);
	builder.addStringValue(mVideoExtStreamId, mCurLayerName);
	builder.addBinaryValue(mVideoExtGoogleVLA, mCurGoogleVLA);
}

void RtpExtensionSourceSimulcast::updateForRtx(RtpExtensionBuilder& builder, const std::shared_ptr<Track>& track) const
{
	const auto layer = track->getSimulcastLayer();

	if (const auto id = mVideoExtMediaId; id != 0) {
		if (!builder.contains(id)) {
			builder.addStringValue(id, track->getMediaId());
		}
	}
	if (const auto id = mVideoExtRepairedStreamId; id != 0) {
		if (!builder.contains(id)) {
			builder.addStringValue(id, layer->name);
		}
	}
}

} // namespace srtc

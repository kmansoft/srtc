#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/rtp_extension_source.h"
#include "srtc/simulcast_layer.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace srtc
{

class Track;
class ByteBuffer;
class Packetizer;
class RtpExtensionBuilder;

class RtpExtensionSourceSimulcast : public RtpExtensionSource
{
public:
	RtpExtensionSourceSimulcast(uint8_t nVideoExtMediaId,
								uint8_t nVideoExtStreamId,
								uint8_t nVideoExtRepairedStreamId,
								uint8_t nVideoExtGoogleVLA);
	~RtpExtensionSourceSimulcast() override;

	static std::shared_ptr<RtpExtensionSourceSimulcast> factory(bool isVideoSimulcast,
																uint8_t nVideoExtMediaId,
																uint8_t nVideoExtStreamId,
																uint8_t nVideoExtRepairedStreamId,
																uint8_t nVideoExtGoogleVLA);

	[[nodiscard]] bool shouldAdd(const std::shared_ptr<Track>& track,
								 const std::shared_ptr<Packetizer>& packetizer,
								 const ByteBuffer& frame);

	void prepare(const std::shared_ptr<Track>& track, const std::vector<std::shared_ptr<SimulcastLayer>>& layerList);
	void clear();

	[[nodiscard]] uint8_t getPadding(const std::shared_ptr<Track>& track, size_t remainingDataSize) override;

	[[nodiscard]] bool wantsExtension(const std::shared_ptr<Track>& track,
									  bool isKeyFrame,
									  int packetNumber) const override;

	void addExtension(RtpExtensionBuilder& builder,
					  const std::shared_ptr<Track>& track,
					  bool isKeyFrame,
					  int packetNumber) override;

	void updateForRtx(RtpExtensionBuilder& builder, const std::shared_ptr<Track>& track) const;

private:
	const uint8_t mVideoExtMediaId;
	const uint8_t mVideoExtStreamId;
	const uint8_t mVideoExtRepairedStreamId;
	const uint8_t mVideoExtGoogleVLA;
	const bool mIsExtensionsValid;

	std::string mCurMediaId;
	std::string mCurLayerName;
	ByteBuffer mCurGoogleVLA;
};

} // namespace srtc
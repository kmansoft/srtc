#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/rtp_extension_source.h"
#include "srtc/simulcast_layer.h"

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
	RtpExtensionSourceSimulcast();
	~RtpExtensionSourceSimulcast() override;

	static std::shared_ptr<RtpExtensionSourceSimulcast> factory(bool isVideoSimulcast);

	[[nodiscard]] bool shouldAdd(const std::shared_ptr<Track>& track,
								 const std::shared_ptr<Packetizer>& packetizer,
								 const ByteBuffer& frame);

	void prepare(const std::shared_ptr<Track>& track, const std::vector<SimulcastLayer>& layerList);
	void clear();

	[[nodiscard]] uint8_t getPadding(const std::shared_ptr<Track>& track, size_t remainingDataSize) override;

	[[nodiscard]] bool wantsExtension(const std::shared_ptr<Track>& track,
									  bool isKeyFrame,
									  unsigned int packetNumber) const override;

	void addExtension(RtpExtensionBuilder& builder,
					  const std::shared_ptr<Track>& track,
					  bool isKeyFrame,
					  unsigned int packetNumber) override;

	void updateForRtx(RtpExtensionBuilder& builder, const std::shared_ptr<Track>& track) const;

private:
	std::string mCurMediaId;
	std::string mCurLayerName;
	ByteBuffer mCurGoogleVLA;

    uint8_t mCurExtMediaId;
    uint8_t mCurExtStreamId;
    uint8_t mCurExtGoogleVLA;
};

} // namespace srtc
#pragma once

#include <cstdint>
#include <memory>

namespace srtc
{

class RtpExtensionBuilder;
class Track;

class RtpExtensionSource
{
public:
	virtual ~RtpExtensionSource();

	[[nodiscard]] virtual uint8_t getPadding(const std::shared_ptr<Track>& track, size_t remainingDataSize) = 0;

	[[nodiscard]] virtual bool wantsExtension(const std::shared_ptr<Track>& track,
											  bool isKeyFrame,
											  unsigned int packetNumber) const = 0;

	virtual void addExtension(RtpExtensionBuilder& builder,
							  const std::shared_ptr<Track>& track,
							  bool isKeyFrame,
							  unsigned int packetNumber) = 0;
};

} // namespace srtc
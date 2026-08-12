#include "srtc/rtp_extension_source_abs_capture_time.h"
#include "srtc/extension_map.h"
#include "srtc/media.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_std_extensions.h"
#include "srtc/sdp_answer.h"
#include "srtc/track.h"

namespace srtc
{

RtpExtensionSourceAbsCaptureTime::RtpExtensionSourceAbsCaptureTime()
    : mAbsCaptureTimeNTP(0u)
    , mExtensionId(0u)
{
}

RtpExtensionSourceAbsCaptureTime::~RtpExtensionSourceAbsCaptureTime() = default;

std::shared_ptr<RtpExtensionSourceAbsCaptureTime> RtpExtensionSourceAbsCaptureTime::factory(
    const std::shared_ptr<SdpAnswer>& answer)
{
    for (const auto& media : answer->getMediaList()) {
        const auto& extensionMap = media->getExtensionMap();
        if (extensionMap.findByName(RtpStandardExtensions::kExtAbsCaptureTime) != 0u) {
            return std::make_shared<RtpExtensionSourceAbsCaptureTime>();
        }
    }

    return {};
}

void RtpExtensionSourceAbsCaptureTime::prepare(const std::shared_ptr<Track>& track, uint64_t absCaptureTimeNTP)
{
    mAbsCaptureTimeNTP = absCaptureTimeNTP;
    mExtensionId = 0;

    if (mAbsCaptureTimeNTP != 0u) {
        const auto media = track->getMedia();
        const auto& extensionMap = media->getExtensionMap();

        mExtensionId = extensionMap.findByName(RtpStandardExtensions::kExtAbsCaptureTime);
    }
}

uint8_t RtpExtensionSourceAbsCaptureTime::getPadding([[maybe_unused]] const std::shared_ptr<Track>& track,
                                                     [[maybe_unused]] size_t remainingDataSize)
{
    return 0u;
}

bool RtpExtensionSourceAbsCaptureTime::wantsExtension([[maybe_unused]] const std::shared_ptr<Track>& track,
                                                      [[maybe_unused]] bool isKeyFrame,
                                                      unsigned int packetNumber) const
{
    return mAbsCaptureTimeNTP != 0u && mExtensionId != 0u && packetNumber == 0;
}

void RtpExtensionSourceAbsCaptureTime::addExtension(RtpExtensionBuilder& builder,
                                                    [[maybe_unused]] const std::shared_ptr<Track>& track,
                                                    [[maybe_unused]] bool isKeyFrame,
                                                    unsigned int packetNumber)
{
    if (mAbsCaptureTimeNTP != 0u && mExtensionId != 0u && packetNumber == 0) {
        builder.addU64Value(mExtensionId, mAbsCaptureTimeNTP);
    }
}

} // namespace srtc

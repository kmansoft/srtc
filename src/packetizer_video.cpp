#include "srtc/packetizer_video.h"
#include "srtc/rtp_extension.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_extension_source.h"

namespace
{

constexpr size_t kMinPayloadSize = 600;

} // namespace

namespace srtc
{

PacketizerVideo::PacketizerVideo(const std::shared_ptr<Track>& track)
    : Packetizer(track)
{
}

PacketizerVideo::~PacketizerVideo() = default;

uint8_t PacketizerVideo::getPadding(const std::shared_ptr<srtc::Track>& track,
                                    const std::shared_ptr<srtc::RtpExtensionSource>& simulcast,
                                    const std::shared_ptr<srtc::RtpExtensionSource>& twcc,
                                    size_t remainingDataSize)
{
    if (remainingDataSize < 300) {
        return 0;
    }

    uint8_t padding = 0;

    if (simulcast) {
        const auto p = simulcast->getPadding(track, remainingDataSize);
        padding = std::max(padding, p);
    }

    if (twcc) {
        const auto p = twcc->getPadding(track, remainingDataSize);
        padding = std::max(padding, p);
    }

    return padding;
}

srtc::RtpExtension PacketizerVideo::buildExtension(const std::shared_ptr<srtc::Track>& track,
                                                   const std::shared_ptr<srtc::RtpExtensionSource>& simulcast,
                                                   const std::shared_ptr<srtc::RtpExtensionSource>& twcc,
                                                   bool isKeyFrame,
                                                   int packetNumber)
{
    srtc::RtpExtension extension;

    const auto wantsSimulcast = simulcast && simulcast->wantsExtension(track, isKeyFrame, packetNumber);
    const auto wantsTWCC = twcc && twcc->wantsExtension(track, isKeyFrame, packetNumber);

    if (wantsSimulcast || wantsTWCC) {
        srtc::RtpExtensionBuilder builder;

        if (wantsSimulcast) {
            simulcast->addExtension(builder, track, isKeyFrame, packetNumber);
        }
        if (wantsTWCC) {
            twcc->addExtension(builder, track, isKeyFrame, packetNumber);
        }

        extension = builder.build();
    }

    return extension;
}

size_t PacketizerVideo::adjustPacketSize(size_t basicPacketSize, size_t padding, const srtc::RtpExtension& extension)
{
    auto sizeLessPadding = basicPacketSize;
    if (padding > 0 && padding <= basicPacketSize / 2) {
        sizeLessPadding -= padding;
    }

    const auto extensionSize = extension.size();
    if (extensionSize == 0) {
        return sizeLessPadding;
    }

    // We need to be careful with unsigned math
    if (extensionSize + kMinPayloadSize > basicPacketSize) {
        return sizeLessPadding;
    }

    return sizeLessPadding - extensionSize;
}

} // namespace srtc
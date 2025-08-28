#include "srtc/packetizer_av1.h"
#include "srtc/codec_av1.h"
#include "srtc/logging.h"
#include "srtc/rtp_extension.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_extension_source.h"
#include "srtc/rtp_packet.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/rtp_time_source.h"
#include "srtc/track.h"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <list>

#define LOG(level, ...) srtc::log(level, "Packetizer_AV1", __VA_ARGS__)
#include "srtc/packetizer_av1.h"

#include <iostream>
#include <unistd.h>

namespace srtc
{

PacketizerAV1::PacketizerAV1(const std::shared_ptr<Track>& track)
    : PacketizerVideo(track)
{
}

PacketizerAV1::~PacketizerAV1() = default;

bool PacketizerAV1::isKeyFrame(const ByteBuffer& frame) const
{
    for (av1::ObuParser parser(frame); parser; parser.next()) {
        const auto obuType = parser.currType();
        if (av1::isFrameObuType(obuType)) {
            if (av1::isKeyFrameObu(parser.currData(), parser.currSize())) {
                return true;
            }
        }
    }

    return false;
}

std::list<std::shared_ptr<RtpPacket>> PacketizerAV1::generate(const std::shared_ptr<RtpExtensionSource>& simulcast,
                                                              const std::shared_ptr<RtpExtensionSource>& twcc,
                                                              size_t mediaProtectionOverhead,
                                                              int64_t pts_usec,
                                                              const ByteBuffer& frame)
{
    std::list<std::shared_ptr<RtpPacket>> result;

    static uint32_t n = 0;

    for (av1::ObuParser parser(frame); parser; parser.next()) {
        const auto obuType = parser.currType();
        const auto isFrame = av1::isFrameObuType(obuType);
        const auto isKeyFrame = isFrame && av1::isKeyFrameObu(parser.currData(), parser.currSize());
        std::cout << "AV1 " << std::setw(4) << n << ", pts = " << std::setw(8) << pts_usec
                  << ", OBU type = " << static_cast<int>(obuType) << ", size = " << std::setw(5) << parser.currSize()
                  << ", key = " << isKeyFrame << ", end = " << parser.isAtEnd() << std::endl;
    }

    n += 1;

    //////////

    const auto track = getTrack();

    const auto timeSource = track->getRtpTimeSource();
    const auto packetSource = track->getRtpPacketSource();

    const auto frameTimestamp = timeSource->getFrameTimestamp(pts_usec);

    // We need to know if there is a key frame (new coded video sequence)
    bool isNewCodedVideoSequence = false;
    for (av1::ObuParser parser(frame); parser; parser.next()) {
        const auto obu_type = parser.currType();
        if (av1::isFrameObuType(obu_type) && av1::isKeyFrameObu(parser.currData(), parser.currSize())) {
            isNewCodedVideoSequence = true;
            break;
        }
    }

    if (!isNewCodedVideoSequence) {
        // Debugging - only send key frames
        return result;
    }

    bool isContinuation = false;

    std::unique_ptr<ByteBuffer> payload;
    std::unique_ptr<ByteWriter> writer;

    for (av1::ObuParser parser(frame); parser; parser.next()) {
        const auto obuType = parser.currType();
        auto obuData = parser.currData();
        auto obuSize = parser.currSize();

        // Temporal and spatial ids
        const auto temporalId = parser.currTemporalId();
        const auto spatialId = parser.currSpatialId();
        const auto isNeedHeaderExtension = temporalId != 0 || spatialId != 0;

        while (obuSize > 0) {
            auto writeNow = std::min<size_t>(obuSize, 1200u);

            if (payload) {
                if (payload->size() < 1000u) {
                    writeNow = std::min<size_t>(obuSize, 1200u - payload->size());
                } else {
                    const auto [rollover, sequence] = packetSource->getNextSequence();
                    result.push_back(std::make_shared<RtpPacket>(
                        track, false, rollover, sequence, frameTimestamp, 0, std::move(*payload)));

                    payload.reset();
                    writer.reset();
                }
            }
            if (!payload) {
                payload = std::make_unique<ByteBuffer>();
                writer = std::make_unique<ByteWriter>(*payload);

                // https://aomediacodec.github.io/av1-rtp-spec/#aggregation-header
                // |Z|Y|0 0|N|-|-|-|
                writer->writeU8(((isContinuation ? 1 : 0) << 7) | (isNewCodedVideoSequence ? 1 : 0) << 3);
                isNewCodedVideoSequence = false;
            }

            // Calculate and write size = obu_header() + obu_extension_header() + payload size
            const auto writeSize = 1 + (isNeedHeaderExtension ? 1 : 0) + writeNow;
            writer->writeLEB128(writeSize);

            // https://aomediacodec.github.io/av1-spec/#obu-header-syntax
            writer->writeU8((obuType << 3) | ((isNeedHeaderExtension ? 1 : 0) << 2));

            if (isNeedHeaderExtension) {
                // https://aomediacodec.github.io/av1-spec/#obu-extension-header-syntax
                writer->writeU8((temporalId << 5) | (spatialId << 3));
            }

            // Payload
            writer->write(obuData, writeNow);

            // Was this partial?
            isContinuation = writeNow < obuSize;
            if (isContinuation) {
                payload->data()[0] |= (1 << 6);

                const auto [rollover, sequence] = packetSource->getNextSequence();
                result.push_back(std::make_shared<RtpPacket>(
                    track, false, rollover, sequence, frameTimestamp, 0, std::move(*payload)));

                payload.reset();
                writer.reset();
            }

            // Advance
            obuData += writeNow;
            obuSize -= writeNow;
        }
    }

    if (payload) {
        const auto [rollover, sequence] = packetSource->getNextSequence();
        result.push_back(
            std::make_shared<RtpPacket>(track, true, rollover, sequence, frameTimestamp, 0, std::move(*payload)));
    }

    for (const auto& item : result) {
        std::cout << "Payload size = " << item->getPayloadSize() << std::endl;
    }

    return result;
}

} // namespace srtc

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

//#define VERBOSE_LOGGING

namespace
{

#ifdef VERBOSE_LOGGING

void dumpFrame(int64_t pts_usec, const srtc::ByteBuffer& frame)
{
    std::printf("PUB AV1 Frame: ts = %" PRIu64 "\n", pts_usec);

    for (srtc::av1::ObuParser parser(frame); parser; parser.next()) {
        const auto obuType = parser.currType();
        const auto obuData = parser.currData();
        const auto obuSize = parser.currSize();
        const auto isKeyFrame = srtc::av1::isKeyFrameObu(obuType, obuData, obuSize);

        std::printf("PUB AV1 OBU: type = %2u, key = %d, size = %4zu\n", obuType, isKeyFrame, parser.currSize());
    }
}

#endif

} // namespace

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
        if (obuType == av1::ObuType::SequenceHeader) {
            return true;
        }
        if (av1::isKeyFrameObu(obuType, parser.currData(), parser.currSize())) {
            return true;
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

    const auto track = getTrack();

    const auto timeSource = track->getRtpTimeSource();
    const auto packetSource = track->getRtpPacketSource();

    const auto frameTimestamp = timeSource->getFrameTimestamp(pts_usec);

    // We need to know if there is a key frame (new coded video sequence)
    bool isNewCodedVideoSequence = false;
    for (av1::ObuParser parser(frame); parser; parser.next()) {
        if (parser.currType() == av1::ObuType::SequenceHeader) {
            isNewCodedVideoSequence = true;
            break;
        }
    }

    // The Y/Z bits
    bool isContinuation = false;

    // We will try to pack multiple OBUs into a single packet, and may also need to split OBUs into multiple packets
    ByteBuffer payload;
    ByteWriter writer(payload);

    // The "-4" is for the OBU headers and the LEB128 size
    const auto basicPacketSize = getBasicPacketSize(mediaProtectionOverhead) - 4;

    RtpExtension extension;
    size_t usablePayloadSize = 0;
    uint8_t padding = 0;
    auto packetNumber = 0u;

#ifdef VERBOSE_LOGGING
    dumpFrame(pts_usec, frame);
#endif

    for (av1::ObuParser parser(frame); parser; parser.next()) {
        const auto obuType = parser.currType();
        if (obuType == av1::ObuType::TemporalDelimiter) {
            // https://aomediacodec.github.io/av1-rtp-spec/#packetization
            continue;
        }

        auto obuCurrData = parser.currData();
        auto obuCurrSize = parser.currSize();

        // Temporal and spatial ids
        const auto temporalId = parser.currTemporalId();
        const auto spatialId = parser.currSpatialId();
        const auto isNeedHeaderExtension = temporalId != 0 || spatialId != 0;

        while (obuCurrSize > 0) {
            if (payload.empty()) {
                extension = buildExtension(track, simulcast, twcc, isNewCodedVideoSequence, packetNumber);
                padding = getPadding(track, simulcast, twcc, basicPacketSize);
                usablePayloadSize = adjustPacketSize(basicPacketSize, padding, extension);
            }

            auto writeNow = std::min<size_t>(obuCurrSize, usablePayloadSize);

            if (!payload.empty()) {
                const auto payloadSizeSoFar = payload.size();
                if (payloadSizeSoFar + 100u < usablePayloadSize) {
                    writeNow = std::min<size_t>(obuCurrSize, usablePayloadSize - payloadSizeSoFar);
                } else {
                    const auto [rollover, sequence] = packetSource->getNextSequence();
                    result.push_back(std::make_shared<RtpPacket>(track,
                                                                 false,
                                                                 rollover,
                                                                 sequence,
                                                                 frameTimestamp,
                                                                 padding,
                                                                 std::move(extension),
                                                                 std::move(payload)));

                    payload.clear();
                    packetNumber += 1;
                }
            }

            if (payload.empty()) {
                // Start a new packet
                // https://aomediacodec.github.io/av1-rtp-spec/#aggregation-header
                // |Z|Y|0 0|N|-|-|-|
                writer.writeU8(((isContinuation ? 1 : 0) << 7) | (isNewCodedVideoSequence ? 1 : 0) << 3);
                isNewCodedVideoSequence = false;
            }

            // When splitting an OBU across multiple packets, only the first packet has the OBU headers
            const auto isFirstPacketFromOBU = obuCurrData == parser.currData();

            // Calculate and write size = obu_header() + obu_extension_header() + payload size
            const auto writeSize = (isFirstPacketFromOBU ? 1 : 0) + (isNeedHeaderExtension ? 1 : 0) + writeNow;
            writer.writeLEB128(writeSize);

            if (isFirstPacketFromOBU) {
                // https://aomediacodec.github.io/av1-spec/#obu-header-syntax
                writer.writeU8((obuType << 3) | ((isNeedHeaderExtension ? 1 : 0) << 2));

                if (isNeedHeaderExtension) {
                    // https://aomediacodec.github.io/av1-spec/#obu-extension-header-syntax
                    writer.writeU8((temporalId << 5) | (spatialId << 3));
                }
            }

            // Payload
            writer.write(obuCurrData, writeNow);

            // Was this partial?
            isContinuation = writeNow < obuCurrSize;
            if (isContinuation) {
                payload.data()[0] |= (1 << 6);

                const auto [rollover, sequence] = packetSource->getNextSequence();
                result.push_back(std::make_shared<RtpPacket>(track,
                                                             false,
                                                             rollover,
                                                             sequence,
                                                             frameTimestamp,
                                                             padding,
                                                             std::move(extension),
                                                             std::move(payload)));

                payload.clear();
                packetNumber += 1;
            }

            // Advance
            obuCurrData += writeNow;
            obuCurrSize -= writeNow;
        }
    }

    if (!payload.empty()) {
        const auto [rollover, sequence] = packetSource->getNextSequence();
        result.push_back(std::make_shared<RtpPacket>(
            track, true, rollover, sequence, frameTimestamp, padding, std::move(extension), std::move(payload)));
    }

    return result;
}

} // namespace srtc

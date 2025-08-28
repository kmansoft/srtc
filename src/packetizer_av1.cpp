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
        const auto obu_type = parser.currType();
        if (av1::isFrameObuType(obu_type)) {
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
        const auto obu_type = parser.currType();
        const auto is_frame = av1::isFrameObuType(obu_type);
        const auto is_key_frame = is_frame && av1::isKeyFrameObu(parser.currData(), parser.currSize());
        std::cout << "AV1 " << std::setw(4) << n << ", pts = " << std::setw(8) << pts_usec
                  << ", OBU type = " << static_cast<int>(obu_type) << ", size = " << std::setw(5) << parser.currSize()
                  << ", key = " << is_key_frame << ", end = " << parser.isAtEnd() << std::endl;
    }

    n += 1;

    //////////

    const auto track = getTrack();

    const auto timeSource = track->getRtpTimeSource();
    const auto packetSource = track->getRtpPacketSource();

    const auto frameTimestamp = timeSource->getFrameTimestamp(pts_usec);

    // We need to know if there is a key frame (new coded video sequence)
    bool newCodedVideoSequence = false;
    for (av1::ObuParser parser(frame); parser; parser.next()) {
        const auto obu_type = parser.currType();
        if (av1::isFrameObuType(obu_type) && av1::isKeyFrameObu(parser.currData(), parser.currSize())) {
            newCodedVideoSequence = true;
            break;
        }
    }

    ByteBuffer payload;
    ByteWriter writer(payload);

    // https://aomediacodec.github.io/av1-rtp-spec/#aggregation-header
    // |Z|Y|0 0|N|-|-|-|
    writer.writeU8((newCodedVideoSequence ? 1 : 0) << 3);

    for (av1::ObuParser parser(frame); parser; parser.next()) {
        const auto obu_type = parser.currType();
        const auto obu_data = parser.currData();
        const auto obu_size = parser.currSize();

        // Temporal and spatial ids
        const auto temporalId = parser.currTemporalId();
        const auto spatialId = parser.currSpatialId();
        const auto needHeaderExtension = temporalId != 0 || spatialId != 0;

        // Calculate and write size = obu_header() + obu_extension_header() + payload size
        const auto writeSize = 1 + (needHeaderExtension ? 1 : 0) + obu_size;
        writer.writeLEB128(writeSize);

        // https://aomediacodec.github.io/av1-spec/#obu-header-syntax
        writer.writeU8((obu_type << 3) | ((needHeaderExtension ? 1 : 0) << 2));

        if (needHeaderExtension) {
            // https://aomediacodec.github.io/av1-spec/#obu-extension-header-syntax
            writer.writeU8((temporalId << 5) | (spatialId << 3));
        }

        // payload
        writer.write(obu_data, obu_size);
    }

    const auto [rollover, sequence] = packetSource->getNextSequence();

    result.push_back(
        std::make_shared<RtpPacket>(track, true, rollover, sequence, frameTimestamp, 0, std::move(payload)));

    for (const auto& item : result) {
        std::cout << "Packet size = " << item->getPayloadSize() << std::endl;
    }

    return result;
}

} // namespace srtc

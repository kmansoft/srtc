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
        const auto obu_size = parser.currSize();
        const auto is_frame = av1::isFrameObuType(obu_type);
        const auto is_key_frame = is_frame && av1::isKeyFrameObu(parser.currData(), parser.currSize());
        std::cout << "AV1 " << std::setw(4) << n << ", pts = " << std::setw(8) << pts_usec
                  << ", OBU type = " << static_cast<int>(obu_type) << ", size = " << std::setw(5) << obu_size
                  << ", key = " << is_key_frame << std::endl;
    }

    n += 1;

    return result;
}

} // namespace srtc

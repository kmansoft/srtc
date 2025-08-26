#include "media_reader_h265.h"
#include "srtc/codec_h265.h"

#include <iomanip>
#include <iostream>

MediaReaderH265::MediaReaderH265(const std::string& filename)
    : MediaReader(filename)
{
}

MediaReaderH265::~MediaReaderH265() = default;

LoadedMedia MediaReaderH265::loadMedia(bool print_info) const
{
    const auto data = loadFile();

    if (print_info) {
        printInfo(data);
    }

    LoadedMedia loaded_media = {};
    loaded_media.codec = srtc::Codec::H265;

    int64_t pts_usec = 0;

    srtc::ByteBuffer vps, sps, pps;
    srtc::ByteBuffer frame;
    uint8_t frame_nalu_type = {};

    for (srtc::h265::NaluParser parser(data); parser; parser.next()) {
        const auto nalu_type = parser.currType();
        switch (nalu_type) {
        case srtc::h265::NaluType::VPS:
            vps.assign(parser.currNalu(), parser.currNaluSize());
            break;
        case srtc::h265::NaluType::SPS:
            sps.assign(parser.currNalu(), parser.currNaluSize());
            break;
        case srtc::h265::NaluType::PPS:
            pps.assign(parser.currNalu(), parser.currNaluSize());
            break;
        default:
            if (srtc::h265::isFrameStart(parser.currData(), parser.currDataSize())) {
                if (!frame.empty()) {
                    LoadedFrame loaded_frame = {};
                    loaded_frame.pts_usec = pts_usec;
                    pts_usec += 1000 * 40; // 25 fps

                    if (srtc::h265::isKeyFrame(frame_nalu_type)) {
                        loaded_frame.csd.push_back(vps.copy());
                        loaded_frame.csd.push_back(sps.copy());
                        loaded_frame.csd.push_back(pps.copy());
                    }

                    loaded_frame.frame = std::move(frame);
                    loaded_media.frame_list.push_back(std::move(loaded_frame));
                    frame.clear();
                }
            }
            frame_nalu_type = parser.currType();
            frame.append(parser.currNalu(), parser.currNaluSize());
            break;
        }
    }

    if (!frame.empty()) {
        LoadedFrame loaded_frame = {};
        loaded_frame.pts_usec = pts_usec;

        if (srtc::h265::isKeyFrame(frame_nalu_type)) {
            loaded_frame.csd.push_back(vps.copy());
            loaded_frame.csd.push_back(sps.copy());
            loaded_frame.csd.push_back(pps.copy());
        }

        loaded_frame.frame = std::move(frame);
        loaded_media.frame_list.push_back(std::move(loaded_frame));
        frame.clear();
    }

    return std::move(loaded_media);
}

void MediaReaderH265::printInfo(const srtc::ByteBuffer& buf) const
{
    size_t all_nalu_count = 0;
    size_t frame_nalu_count = 0;
    size_t all_frame_count = 0;
    size_t key_frame_count = 0;

    size_t vps_count = 0;
    size_t sps_count = 0;
    size_t pps_count = 0;

    for (srtc::h265::NaluParser parser(buf); parser; parser.next()) {
        all_nalu_count += 1;

        const auto nalu_type = parser.currType();
        switch (nalu_type) {
        case srtc::h265::NaluType::VPS:
            vps_count += 1;
            break;
        case srtc::h265::NaluType::SPS:
            sps_count += 1;
            break;
        case srtc::h265::NaluType::PPS:
            pps_count += 1;
            break;
        default:
            frame_nalu_count += 1;
            if (srtc::h265::isFrameStart(parser.currData(), parser.currDataSize())) {
                all_frame_count += 1;
                if (srtc::h265::isKeyFrame(nalu_type)) {
                    key_frame_count += 1;
                }
            }
        }
    }

    std::cout << "*** NALU count (all):   " << std::setw(4) << all_nalu_count << std::endl;
    std::cout << "*** NALU count (frame): " << std::setw(4) << frame_nalu_count << std::endl;
    std::cout << "*** VPS count:          " << std::setw(4) << vps_count << std::endl;
    std::cout << "*** SPS count:          " << std::setw(4) << sps_count << std::endl;
    std::cout << "*** PPS count:          " << std::setw(4) << pps_count << std::endl;
    std::cout << "*** Frame count (all):  " << std::setw(4) << all_frame_count << std::endl;
    std::cout << "*** Frame count (key):  " << std::setw(4) << key_frame_count << std::endl;
}

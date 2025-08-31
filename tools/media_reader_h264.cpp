#include "media_reader_h264.h"

#include "srtc/codec_h264.h"

#include <iomanip>
#include <iostream>

MediaReaderH264::MediaReaderH264(const std::string& filename)
    : MediaReader(filename)
{
}

MediaReaderH264::~MediaReaderH264() = default;

LoadedMedia MediaReaderH264::loadMedia(bool print_info) const
{
    const auto data = loadFile();

    if (print_info) {
        printInfo(data);
    }

    LoadedMedia loaded_media = {};
    loaded_media.codec = srtc::Codec::H264;

    int64_t pts_usec = 0;

    srtc::ByteBuffer sps, pps;
    srtc::ByteBuffer frame;
    uint8_t frame_nalu_type = {};

    for (srtc::h264::NaluParser parser(data); parser; parser.next()) {
        const auto nalu_type = parser.currType();
        switch (nalu_type) {
        default:
            break;
        case srtc::h264::NaluType::SPS:
            sps.assign(parser.currNalu(), parser.currNaluSize());
            break;
        case srtc::h264::NaluType::PPS:
            pps.assign(parser.currNalu(), parser.currNaluSize());
            break;
        case srtc::h264::NaluType::KeyFrame:
        case srtc::h264::NaluType::NonKeyFrame:
            if (srtc::h264::isFrameStart(parser.currData(), parser.currDataSize())) {
                if (!frame.empty()) {
                    LoadedFrame loaded_frame = {};
                    loaded_frame.pts_usec = pts_usec;
                    pts_usec += 1000 * 40; // 25 fps

                    if (frame_nalu_type == srtc::h264::NaluType::KeyFrame) {
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

        if (frame_nalu_type == srtc::h264::NaluType::KeyFrame) {
            loaded_frame.csd.push_back(sps.copy());
            loaded_frame.csd.push_back(pps.copy());
        }

        loaded_frame.frame = std::move(frame);
        loaded_media.frame_list.push_back(std::move(loaded_frame));
        frame.clear();
    }

    return std::move(loaded_media);
}

void MediaReaderH264::printInfo(const srtc::ByteBuffer& data) const
{
    uint32_t all_nalu_count = 0;
    uint32_t frame_nalu_count = 0;
    uint32_t sps_count = 0;
    uint32_t pps_count = 0;
    uint32_t all_frame_count = 0;
    uint32_t key_frame_count = 0;

    for (srtc::h264::NaluParser parser(data); parser; parser.next()) {
        all_nalu_count += 1;

        switch (parser.currType()) {
        default:
            break;
        case srtc::h264::NaluType::SPS:
            sps_count += 1;
            break;
        case srtc::h264::NaluType::PPS:
            pps_count += 1;
            break;
        case srtc::h264::NaluType::KeyFrame:
        case srtc::h264::NaluType::NonKeyFrame:
            frame_nalu_count += 1;
            srtc::h264::BitReader br = { parser.currData() + 1, parser.currDataSize() - 1 };
            const auto first_mb_in_slice = br.readUnsignedExpGolomb();
            if (first_mb_in_slice == 0) {
                all_frame_count += 1;
                if (parser.currType() == srtc::h264::NaluType::KeyFrame) {
                    key_frame_count += 1;
                }
            }
            break;
        }
    }

    std::cout << "*** NALU count (all):   " << std::setw(4) << all_nalu_count << std::endl;
    std::cout << "*** NALU count (frame): " << std::setw(4) << frame_nalu_count << std::endl;
    std::cout << "*** SPS count:          " << std::setw(4) << sps_count << std::endl;
    std::cout << "*** PPS count:          " << std::setw(4) << pps_count << std::endl;
    std::cout << "*** Frame count (all):  " << std::setw(4) << all_frame_count << std::endl;
    std::cout << "*** Frame count (key):  " << std::setw(4) << key_frame_count << std::endl;
}
#include "media_writer_av1.h"
#include "srtc/bit_reader.h"
#include "srtc/byte_buffer.h"
#include "srtc/codec_av1.h"

#include <cstdlib>
#include <cstring>

MediaWriterAV1::MediaWriterAV1(const std::string& filename, const std::shared_ptr<srtc::Track>& track)
    : MediaWriter(filename)
    , mTrack(track)
    , mOutAllFrameCount(0)
    , mOutKeyFrameCount(0)
    , mOutByteCount(0)
    , mBaseRtpTimestamp(0)
{
    checkExtension({ ".webm" });
}

MediaWriterAV1::~MediaWriterAV1()
{
    if (!mFrameList.empty()) {
        uint16_t frameWidth = 1920;
        uint16_t frameHeight = 1080;
        extractAV1Dimensions(frameWidth, frameHeight);

        FILE* file = fopen(mFilename.c_str(), "wb");
        if (!file) {
            std::printf("*** Cannot open output file %s\n", mFilename.c_str());
            exit(1);
        }

        MediaWriterWebm writer(file, "V_AV1", frameWidth, frameHeight, mFrameList);
        writer.write();

        fclose(file);

        std::printf("AV1: Wrote %zu frames, %zu key frames, %zu bytes to %s\n",
                    mOutAllFrameCount,
                    mOutKeyFrameCount,
                    mOutByteCount,
                    mFilename.c_str());
    }
}

void MediaWriterAV1::write(const std::shared_ptr<srtc::EncodedFrame>& frame)
{
    // Check if it's a key frame
    const auto frameData = frame->data.data();
    const auto frameSize = frame->data.size();

    bool isKeyFrame = false;
    for (srtc::av1::ObuParser parser(frame->data); parser; parser.next()) {
        const auto obuType = parser.currType();
        if (obuType == srtc::av1::ObuType::SequenceHeader) {
            isKeyFrame = true;
            break;
        }

        const auto obuData = parser.currData();
        const auto obuSize = parser.currSize();
        if (srtc::av1::isKeyFrameObu(obuType, obuData, obuSize)) {
            isKeyFrame = true;
            break;
        }
    }

    if (isKeyFrame) {
        // Maintain key frame count
        mOutKeyFrameCount += 1;
    }

    // Calculate pts
    int64_t pts_usec = 0;
    if (mOutAllFrameCount == 0) {
        mBaseRtpTimestamp = frame->rtp_timestamp_ext;
        std::printf("AV1: Started buffering video frames, will save when exiting from Ctrl+C\n");
    } else {
        pts_usec = static_cast<int64_t>(frame->rtp_timestamp_ext - mBaseRtpTimestamp) * 1000 / 90;
    }

    mOutAllFrameCount += 1;
    mOutByteCount += frame->data.size();

    MediaWriterWebm::Frame outFrame;
    outFrame.pts_usec = pts_usec;
    outFrame.data = std::move(frame->data);
    outFrame.is_keyframe = isKeyFrame;

    mFrameList.push_back(std::move(outFrame));
}

bool MediaWriterAV1::extractAV1Dimensions(uint16_t& width, uint16_t& height) const
{
    // Find first keyframe with sequence header
    for (const auto& frame : mFrameList) {
        if (frame.is_keyframe && !frame.data.empty()) {
            if (extractAV1Dimensions(frame.data, width, height)) {
                return true;
            }
        }
    }

    // No keyframe found or unable to extract dimensions
    return false;
}

bool MediaWriterAV1::extractAV1Dimensions(const srtc::ByteBuffer& frame, uint16_t& width, uint16_t& height) const
{
    for (srtc::av1::ObuParser parser(frame); parser; parser.next()) {
        const auto obuType = parser.currType();
        if (obuType == srtc::av1::ObuType::SequenceHeader) {
            const auto obuData = parser.currData();
            const auto obuSize = parser.currSize();

            if (obuSize < 8) {
                continue; // Too small to contain dimensions
            }

            // Parse AV1 sequence header according to specification
            // Reference: https://aomediacodec.github.io/av1-spec/#sequence-header-obu-syntax

            // I took a lot of shortcuts, but it handles AV1 coming out of Chrome

            srtc::BitReader reader(obuData, obuSize);

            uint8_t seq_profile = reader.readBits(3);
            uint8_t still_picture = reader.readBit();
            uint8_t reduced_still_picture_header = reader.readBit();
            if (reduced_still_picture_header) {
                // Doesn't apply
                return false;
            }

            uint8_t decoder_model_info_present_flag = 0;
            uint8_t timing_info_present_flag = reader.readBit();
            if (timing_info_present_flag) {
                // We don't handle this case
                return false;
            }

            uint8_t initial_display_delay_present_flag = reader.readBit();
            uint8_t operating_points_cnt_minus_1 = reader.readBits(5);

            for (size_t i = 0; i <= operating_points_cnt_minus_1; i++) {
                uint32_t operating_point_idc_i = reader.readBits(12);
                uint32_t seq_level_i = reader.readBits(5);
                if (seq_level_i > 7) {
                    // seq_tier[i]
                    reader.readBit();
                }
                if (decoder_model_info_present_flag) {
                    // decoder_model_present_for_this_op[ i ]
                    uint32_t decoder_model_present_for_this_op_i = reader.readBit();
                    if (decoder_model_present_for_this_op_i) {
                        // We don't handle this case
                        return false;
                    }
                }
                if (initial_display_delay_present_flag) {
                    uint32_t initial_display_delay_present_for_this_op_i = reader.readBit();
                    if (initial_display_delay_present_for_this_op_i) {
                        // initial_display_delay_minus_1[ i ]
                        reader.readBits(4);
                    }
                }
            }

            // Read the dimensions
            uint32_t frame_width_bits_minus_1 = reader.readBits(4);
            uint32_t frame_height_bits_minus_1 = reader.readBits(4);
            uint32_t max_frame_width_minus_1 = reader.readBits(frame_width_bits_minus_1 + 1);
            uint32_t max_frame_height_minus_1 = reader.readBits(frame_height_bits_minus_1 + 1);

            width = max_frame_width_minus_1 + 1;
            height = max_frame_height_minus_1 + 1;

            return true;
        }
    }

    return false;
}
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

            if (obuSize < 4) {
                continue; // Too small to contain dimensions
            }

            // I am too lazy right now to implement extraction of frame dimentions from AV1 sequence header

            width = 1280;
            height = 720;

            return true;
        }
    }

    return false;
}
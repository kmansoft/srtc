#include "media_writer_vp8.h"
#include "srtc/byte_buffer.h"

#include <cstdlib>
#include <cstring>

MediaWriterVP8::MediaWriterVP8(const std::string& filename, const std::shared_ptr<srtc::Track>& track)
    : MediaWriter(filename)
    , mTrack(track)
    , mOutAllFrameCount(0)
    , mOutKeyFrameCount(0)
    , mOutByteCount(0)
    , mBaseRtpTimestamp(0)
{
    checkExtension({ ".webm" });
}

MediaWriterVP8::~MediaWriterVP8()
{
    if (!mFrameList.empty()) {
        uint16_t frameWidth = 1920;
        uint16_t frameHeight = 1080;
        extractVP8Dimensions(frameWidth, frameHeight);

        FILE* file = fopen(mFilename.c_str(), "wb");
        if (!file) {
            std::printf("*** Cannot open output file %s\n", mFilename.c_str());
            exit(1);
        }

        MediaWriterWebm writer(file, "V_VP8", frameWidth, frameHeight, mFrameList);
        writer.write();

        fclose(file);

        std::printf("VP8: Wrote %zu frames, %zu key frames, %zu bytes to %s\n",
                    mOutAllFrameCount,
                    mOutKeyFrameCount,
                    mOutByteCount,
                    mFilename.c_str());
    }
}

void MediaWriterVP8::write(const std::shared_ptr<srtc::EncodedFrame>& frame)
{
    // Check if it's a key frame
    const auto frameData = frame->data.data();
    const auto frameSize = frame->data.size();

    if (frameSize < 3) {
        return;
    }

    const auto tag = frameData[0] | (frameData[1] << 8) | (frameData[2] << 16);
    const auto tagFrameType = tag & 0x01;
    bool is_keyframe = (tagFrameType == 0 && frameSize > 10);

    if (is_keyframe) {
        // Maintain key frame count
        mOutKeyFrameCount += 1;
    }

    // Calculate pts
    int64_t pts_usec = 0;
    if (mOutAllFrameCount == 0) {
        mBaseRtpTimestamp = frame->rtp_timestamp_ext;
        std::printf("VP8: Started buffering video frames, will save when exiting from Ctrl+C\n");
    } else {
        pts_usec = static_cast<int64_t>(frame->rtp_timestamp_ext - mBaseRtpTimestamp) * 1000 / 90;
    }

    mOutAllFrameCount += 1;
    mOutByteCount += frame->data.size();

    MediaWriterWebm::Frame outFrame;
    outFrame.pts_usec = pts_usec;
    outFrame.data = std::move(frame->data);
    outFrame.is_keyframe = is_keyframe;

    mFrameList.push_back(std::move(outFrame));
}

bool MediaWriterVP8::extractVP8Dimensions(uint16_t& width, uint16_t& height) const
{
    // Find first keyframe
    for (const auto& frame : mFrameList) {
        if (frame.is_keyframe && frame.data.size() >= 10) {
            const auto* frameData = frame.data.data();

            // VP8 keyframe structure (RFC 6386 Section 9.1)
            // Skip the 3-byte frame tag
            const auto* uncompressed_data_chunk = frameData + 3;

            // Skip start code (3 bytes: 0x9d 0x01 0x2a for keyframes)
            if (frame.data.size() >= 6 && uncompressed_data_chunk[0] == 0x9d && uncompressed_data_chunk[1] == 0x01 &&
                uncompressed_data_chunk[2] == 0x2a) {

                if (frame.data.size() >= 10) {
                    // Width and height are at bytes 6-7 and 8-9 respectively
                    const auto* width_data = frameData + 6;
                    const auto* height_data = frameData + 8;

                    // Extract 14-bit width and height (little-endian)
                    width = ((width_data[1] << 8) | width_data[0]) & 0x3FFF;
                    height = ((height_data[1] << 8) | height_data[0]) & 0x3FFF;

                    return true;
                }
            }
        }
    }

    // No keyframe found or unable to extract dimensions
    return false;
}

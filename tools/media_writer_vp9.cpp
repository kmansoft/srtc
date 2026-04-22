#include "media_writer_vp9.h"
#include "srtc/codec_vp9.h"

#include <cstdlib>

MediaWriterVP9::MediaWriterVP9(const std::string& filename, const std::shared_ptr<srtc::Track>& track)
    : MediaWriter(filename)
    , mTrack(track)
    , mOutAllFrameCount(0)
    , mOutKeyFrameCount(0)
    , mOutByteCount(0)
    , mBaseRtpTimestamp(0)
{
    checkExtension({ ".webm" });
}

MediaWriterVP9::~MediaWriterVP9()
{
    if (!mFrameList.empty()) {
        uint16_t frameWidth = 1920;
        uint16_t frameHeight = 1080;
        extractVP9Dimensions(frameWidth, frameHeight);

        FILE* file = fopen(mFilename.c_str(), "wb");
        if (!file) {
            std::printf("*** Cannot open output file %s\n", mFilename.c_str());
            exit(1);
        }

        MediaWriterWebm writer(file, "V_VP9", frameWidth, frameHeight, mFrameList);
        writer.write();

        fclose(file);

        std::printf("VP9: Wrote %zu frames, %zu key frames, %zu bytes to %s\n",
                    mOutAllFrameCount,
                    mOutKeyFrameCount,
                    mOutByteCount,
                    mFilename.c_str());
    }
}

void MediaWriterVP9::write(const std::shared_ptr<srtc::EncodedFrame>& frame)
{
    const auto frameData = frame->data.data();
    const auto frameSize = frame->data.size();

    const bool isKeyFrame = srtc::vp9::isKeyFrame(frameData, frameSize);

    if (isKeyFrame) {
        mOutKeyFrameCount += 1;
    }

    int64_t pts_usec = 0;
    if (mOutAllFrameCount == 0) {
        mBaseRtpTimestamp = frame->rtp_timestamp_ext;
        std::printf("VP9: Started buffering video frames, will save when exiting from Ctrl+C\n");
    } else {
        pts_usec = static_cast<int64_t>(frame->rtp_timestamp_ext - mBaseRtpTimestamp) * 1000 / 90;
    }

    mOutAllFrameCount += 1;
    mOutByteCount += frameSize;

    MediaWriterWebm::Frame outFrame;
    outFrame.pts_usec = pts_usec;
    outFrame.data = std::move(frame->data);
    outFrame.is_keyframe = isKeyFrame;

    mFrameList.push_back(std::move(outFrame));
}

bool MediaWriterVP9::extractVP9Dimensions(uint16_t& width, uint16_t& height) const
{
    for (const auto& frame : mFrameList) {
        if (frame.is_keyframe && !frame.data.empty()) {
            if (srtc::vp9::extractDimensions(frame.data.data(), frame.data.size(), width, height)) {
                return true;
            }
        }
    }
    return false;
}

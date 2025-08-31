#include "srtc/encoded_frame.h"

#include "media_writer_h26x.h"

MediaWriterH26x::MediaWriterH26x(const std::string& filename, const std::shared_ptr<srtc::Track>& track)
    : MediaWriter(filename)
    , mTrack(track)
    , mFile(nullptr)
    , mOutFrameCount(0)
    , mOutByteCount(0)
{
    checkExtension({ ".h264", ".h265" });
}

MediaWriterH26x::~MediaWriterH26x()
{
    if (mFile) {
        fclose(mFile);
        mFile = nullptr;

        std::printf("H26x: Wrote %zu frames, %zu bytes to %s\n", mOutFrameCount, mOutByteCount, mFilename.c_str());
    }
}

void MediaWriterH26x::write(const std::shared_ptr<srtc::EncodedFrame>& frame)
{
    if (!mFile) {
        // Open the file
        mFile = fopen(mFilename.c_str(), "wb");
        if (!mFile) {
            std::printf("H26x: Cannot open output file %s\n", mFilename.c_str());
            return;
        }

        std::printf("H26x: Opened output file %s\n", mFilename.c_str());
    }

    if (!frame->data.empty()) {
        const auto& data = frame->data;

        fwrite(data.data(), data.size(), 1, mFile);
        fflush(mFile);

        mOutFrameCount += 1;
        mOutByteCount += data.size();
    }
}
#include "media_writer_vp8.h"

MediaWriterVP8::MediaWriterVP8(const std::string& filename, const std::shared_ptr<srtc::Track>& track)
    : MediaWriter(filename)
    , mTrack(track)
    , mOutFrameCount(0)
    , mOutByteCount(0)
{
    checkExtension({ ".webm" });
}

MediaWriterVP8::~MediaWriterVP8()
{
    if (mOutFrameCount > 0 && mOutByteCount > 0) {
        std::printf("VP8: Wrote %zu frames, %zu bytes to %s\n", mOutFrameCount, mOutByteCount, mFilename.c_str());
    }
}

void MediaWriterVP8::write(const std::shared_ptr<srtc::EncodedFrame>& frame)
{
    mOutFrameCount += 1;
    mOutByteCount += frame->data.size();

#if 1
    const auto frameData = frame->data.data();
    const auto frameSize = frame->data.size();

    if (frameSize < 3) {
        return;
    }

    // Check if it's a key frame
    const auto tag = frameData[0] | (frameData[1] << 8) | (frameData[2] << 16);
    const auto tagFrameType = tag & 0x01;

    if (tagFrameType == 0 && frameSize > 10) {
        // Decode key frame data
        const auto frame_width_data = frameData + 6;
        const auto frame_width = ((frame_width_data[1] << 8) | frame_width_data[0]) & 0x3FFF;
        const auto frame_height_data = frameData + 8;
        const auto frame_height = ((frame_height_data[1] << 8) | frame_height_data[0]) & 0x3FFF;

        char fname[128];
        std::snprintf(fname, sizeof(fname), "sub-key-frame-%zu.ivf", mOutFrameCount);

        const auto file = std::fopen(fname, "wb");
        if (file) {
            // Write IVF header (32 bytes)
            std::fwrite("DKIF", 4, 1, file); // signature

            uint16_t version = 0;
            uint16_t header_len = 32;
            std::fwrite(&version, 2, 1, file);
            std::fwrite(&header_len, 2, 1, file);

            std::fwrite("VP80", 4, 1, file); // codec fourcc

            uint16_t width = frame_width;
            uint16_t height = frame_height;
            std::fwrite(&width, 2, 1, file);
            std::fwrite(&height, 2, 1, file);

            uint32_t frame_rate_num = 30;
            uint32_t frame_rate_den = 1;
            uint32_t frame_count = 1;
            uint32_t unused = 0;
            std::fwrite(&frame_rate_num, 4, 1, file);
            std::fwrite(&frame_rate_den, 4, 1, file);
            std::fwrite(&frame_count, 4, 1, file);
            std::fwrite(&unused, 4, 1, file);

            // Write frame header (12 bytes)
            const auto frame_size_u32 = static_cast<uint32_t>(frameSize);
            uint64_t timestamp = 0;
            std::fwrite(&frame_size_u32, 4, 1, file);
            std::fwrite(&timestamp, 8, 1, file);

            // Write VP8 frame data
            std::fwrite(frameData, frameSize, 1, file);

            // Close
            std::fclose(file);
        }
    }
#endif
}

#pragma once

#include "media_writer.h"

#include "srtc/track.h"
#include "srtc/byte_buffer.h"

#include <cstdio>

class MediaWriterVP8 final : public MediaWriter
{
public:
    MediaWriterVP8(const std::string& filename, const std::shared_ptr<srtc::Track>& track);
    ~MediaWriterVP8() override;

protected:
    void write(const std::shared_ptr<srtc::EncodedFrame>& frame) override;

private:
    const std::shared_ptr<srtc::Track> mTrack;
    size_t mOutAllFrameCount;
    size_t mOutKeyFrameCount;
    size_t mOutByteCount;

    struct VP8Frame {
        int64_t pts_usec;
        srtc::ByteBuffer data;
    };
    std::vector<VP8Frame> mFrameList;
    uint64_t mBaseRtpTimestamp;

    void writeWebM();
    static void writeEBMLHeader(FILE* file);
    static void writeSegmentInfo(FILE* file, uint64_t duration_ns);
    void writeTracks(FILE* file);
    void writeClusters(FILE* file);
    static void writeEBMLElement(FILE* file, uint32_t id, const void* data, size_t size);
    static void writeVarInt(FILE* file, uint64_t value);
    static int getVarIntWidth(uint64_t value);
    static bool isKeyFrame(const VP8Frame& frame);
    bool extractVP8Dimensions(uint16_t& width, uint16_t& height) const;
};
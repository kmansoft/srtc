#pragma once

#include "srtc/byte_buffer.h"

#include <cstdio>
#include <vector>

class MediaWriterWebm
{
public:
    struct Frame {
        int64_t pts_usec;
        bool is_keyframe;
        srtc::ByteBuffer data;
    };

    MediaWriterWebm(FILE* file,
                    const std::string& codecId,
                    uint32_t frameWidth,
                    uint32_t frameHeight,
                    const std::vector<Frame>& frameList);

    void write();

    ~MediaWriterWebm();

private:
    FILE* const mFile;
    const std::string mCodecId;
    const uint32_t mFrameWidth;
    const uint32_t mFrameHeight;
    const std::vector<Frame>& mFrameList;

    void writeWebM();
    void writeEBMLHeader();
    static void writeSegmentInfo(FILE* file, uint64_t duration_ns);
    void writeTracks(FILE* file);
    void writeClusters(FILE* file);
    static void writeEBMLElement(FILE* file, uint32_t id, const void* data, size_t size);
    static void writeVarInt(FILE* file, uint64_t value);
    static int getVarIntWidth(uint64_t value);
};
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include "srtc/srtc.h"
#include "srtc/byte_buffer.h"

struct LoadedFrame
{
    int64_t pts_usec;

    std::vector<srtc::ByteBuffer> csd;
    srtc::ByteBuffer frame;
};

struct LoadedMedia
{
    srtc::Codec codec;

    std::vector<LoadedFrame> frame_list;
};

class MediaReader {
protected:
    explicit MediaReader(const std::string& filename);

public:
    static std::shared_ptr<MediaReader> create(const std::string& filename);

    [[nodiscard]] virtual LoadedMedia loadMedia(bool print_info) const = 0;
    virtual ~MediaReader();

protected:
    const std::string mFileName;

    [[nodiscard]] srtc::ByteBuffer loadFile() const;
};
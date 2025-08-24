#pragma once

#include "media_writer.h"

#include "srtc/track.h"

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
    size_t mOutFrameCount;
    size_t mOutByteCount;
};
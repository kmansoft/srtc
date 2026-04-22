#pragma once

#include "media_writer.h"
#include "media_writer_webm.h"

#include "srtc/track.h"
#include "srtc/byte_buffer.h"

class MediaWriterVP9 final : public MediaWriter
{
public:
    MediaWriterVP9(const std::string& filename, const std::shared_ptr<srtc::Track>& track);
    ~MediaWriterVP9() override;

protected:
    void write(const std::shared_ptr<srtc::EncodedFrame>& frame) override;

private:
    const std::shared_ptr<srtc::Track> mTrack;
    size_t mOutAllFrameCount;
    size_t mOutKeyFrameCount;
    size_t mOutByteCount;

    std::vector<MediaWriterWebm::Frame> mFrameList;
    uint64_t mBaseRtpTimestamp;

    bool extractVP9Dimensions(uint16_t& width, uint16_t& height) const;
};

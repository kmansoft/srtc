#pragma once

#include "media_writer.h"
#include "media_writer_webm.h"

#include "srtc/track.h"
#include "srtc/byte_buffer.h"

class MediaWriterAV1 final : public MediaWriter
{
public:
    MediaWriterAV1(const std::string& filename, const std::shared_ptr<srtc::Track>& track);
    ~MediaWriterAV1() override;

protected:
    void write(const std::shared_ptr<srtc::EncodedFrame>& frame) override;

private:
    const std::shared_ptr<srtc::Track> mTrack;
    size_t mOutAllFrameCount;
    size_t mOutKeyFrameCount;
    size_t mOutByteCount;

    std::vector<MediaWriterWebm::Frame> mFrameList;
    uint64_t mBaseRtpTimestamp;

    bool extractAV1Dimensions(uint16_t& width, uint16_t& height) const;
    bool extractAV1Dimensions(const srtc::ByteBuffer& frame, uint16_t& width, uint16_t& height) const;
};
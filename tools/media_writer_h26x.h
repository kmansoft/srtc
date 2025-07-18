#pragma once

#include "media_writer.h"

#include "srtc/track.h"

#include <cstdio>

class MediaWriterH26x final : public MediaWriter
{
public:
	MediaWriterH26x(const std::string& filename, const std::shared_ptr<srtc::Track>& track);
	~MediaWriterH26x() override;

protected:
	void write(const std::shared_ptr<srtc::EncodedFrame> &frame) override;

private:
	const std::shared_ptr<srtc::Track> mTrack;
	FILE* mFile;
	size_t m_outPacketCount;
	size_t m_outByteCount;
	bool mIsSeenKeyFrame;
};
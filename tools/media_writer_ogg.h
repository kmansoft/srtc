#pragma once

#include "media_writer.h"

#include "ogg/ogg.h"

#include "srtc/track.h"

#include <cstdio>

class MediaWriterOgg final : public MediaWriter
{
public:
	MediaWriterOgg(const std::string& filename, const std::shared_ptr<srtc::Track>& track);
	~MediaWriterOgg() override;

protected:
	void write(const std::shared_ptr<srtc::EncodedFrame> &frame) override;

private:
	const std::shared_ptr<srtc::Track> mTrack;
	FILE* mFile;
	ogg_stream_state m_os;
	ogg_page m_og;
	ogg_packet m_op;
	ogg_int64_t m_packetno;
	ogg_int64_t m_granulepos;

	size_t m_outPacketCount;
	size_t m_outPageCount;
	size_t m_outByteCount;
};
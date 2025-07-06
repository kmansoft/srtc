#include "srtc/encoded_frame.h"

#include "media_writer_h26x.h"

MediaWriterH26x::MediaWriterH26x(const std::string& filename, const std::shared_ptr<srtc::Track>& track)
	: MediaWriter(filename)
	, mTrack(track)
	, mFile(nullptr)
	, mIsSeenKeyFrame(false)
{
}

MediaWriterH26x::~MediaWriterH26x()
{
	if (mFile) {
		fclose(mFile);
		mFile = nullptr;

		std::printf("H26x: Wrote %zu frames, %zu bytes to %s\n", m_outPacketCount, m_outByteCount, mFilename.c_str());
	}
}

void MediaWriterH26x::write(const std::shared_ptr<srtc::EncodedFrame>& frame)
{
	if (!mFile) {
		// Open the file
		mFile = fopen(mFilename.c_str(), "wb");
		if (!mFile) {
			perror("H264: Cannot open output file");
			return;
		}

		std::printf("H264: Opened output file %s\n", mFilename.c_str());
	}

	srtc::ByteReader reader(frame->data);
	const auto value = reader.readU8();
	const auto type = value & 0x1F;

	if (type == 1 && !mIsSeenKeyFrame) {
		return;
	} else if (type == 5 || type == 7 || type == 8) {
		mIsSeenKeyFrame = true;
	}

	static constexpr uint8_t kAnnexB[] = { 0, 0, 0, 1 };

	fwrite(kAnnexB, sizeof(kAnnexB), 1, mFile);
	fwrite(frame->data.data(), frame->data.size(), 1, mFile);

	m_outPacketCount += 1;
	m_outByteCount += frame->data.size();
}
#include "srtc/encoded_frame.h"

#include "media_writer_h26x.h"

MediaWriterH26x::MediaWriterH26x(const std::string& filename, const std::shared_ptr<srtc::Track>& track)
	: MediaWriter(filename)
	, mTrack(track)
	, mFile(nullptr)
	, mIsSeenKeyFrame(false)
    , m_outPacketCount(0)
    , m_outByteCount(0)
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
			std::printf("H26x: Cannot open output file %s\n", mFilename.c_str());
			return;
		}

		std::printf("H26x: Opened output file %s\n", mFilename.c_str());
	}

    if (!frame->data.empty()) {
        const auto first = frame->data.data()[0];
        const auto type = first & 0x1F;

        if (type == 1 && !mIsSeenKeyFrame) {
            std::printf("H26x: no key frame yet\n");
            return;
        } else if (!mIsSeenKeyFrame) {
            if (type == 5 || type == 7 || type == 8) {
                std::printf("H26x: Start writing output on a key frame\n");
                mIsSeenKeyFrame = true;
            }
        }

        const auto& data = frame->data;

        static constexpr uint8_t kAnnexB[] = { 0, 0, 0, 1 };

        fwrite(kAnnexB, sizeof(kAnnexB), 1, mFile);
        fwrite(data.data(), data.size(), 1, mFile);

        m_outPacketCount += 1;
        m_outByteCount += data.size();
    }
}
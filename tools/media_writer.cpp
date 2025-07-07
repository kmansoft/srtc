#include "media_writer.h"

MediaWriter::MediaWriter(const std::string& filename)
	: mFilename(filename)
	, mQuit(false)
{
}

MediaWriter::~MediaWriter()
{
	{
		std::lock_guard lock(mMutex);
		mQuit = true;
		mCond.notify_one();
	}

	mThread.join();
}

void MediaWriter::start()
{
	mThread = std::thread(&MediaWriter::threadFunc, this);
}

void MediaWriter::send(const std::shared_ptr<srtc::EncodedFrame>& frame)
{
	std::lock_guard lock(mMutex);
	mQueue.push_back(frame);
	mCond.notify_one();
}

void MediaWriter::threadFunc()
{
	while (true) {
		std::shared_ptr<srtc::EncodedFrame> frame;

		{
			std::unique_lock lock(mMutex);
			mCond.wait(lock, [this] { return mQuit || !mQueue.empty(); });

			if (mQuit) {
				break;
			}
			if (!mQueue.empty()) {
				frame = mQueue.front();
				mQueue.pop_front();
			}
		}

		if (frame) {
			write(frame);
		}
	}
}

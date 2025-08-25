#include "media_writer.h"

#include <iostream>

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

void MediaWriter::checkExtension(const std::initializer_list<const char*>& expected)
{
    const char sep =
#ifdef _WIN32
        '/';
#else
        '\\';
#endif

    const auto index_ext = mFilename.rfind('.');
    const auto index_sep = mFilename.rfind('/');

    std::string ext;
    if (index_ext != std::string::npos) {
        if (index_sep == std::string::npos || index_sep < index_ext) {
            ext = mFilename.substr(index_ext);
        }
    }

    for (const auto& item : expected) {
        if (ext == item) {
            return;
        }
    }

    std::cout << "*** The output file " << mFilename << " has wrong extension for its format" << std::endl;
    exit(1);
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

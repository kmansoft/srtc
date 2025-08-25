#pragma once

#include "srtc/encoded_frame.h"

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class MediaWriter
{
protected:
	explicit MediaWriter(const std::string& filename);
	virtual void write(const std::shared_ptr<srtc::EncodedFrame>& frame) = 0;

	const std::string mFilename;

public:
	virtual ~MediaWriter();

	void start();
	void send(const std::shared_ptr<srtc::EncodedFrame>& frame);

protected:
    void checkExtension(const std::initializer_list<const char*>& expected);

private:
	std::mutex mMutex;
	std::condition_variable mCond;
	std::list<std::shared_ptr<srtc::EncodedFrame>> mQueue;
	bool mQuit;
	std::thread mThread;

	void threadFunc();
};
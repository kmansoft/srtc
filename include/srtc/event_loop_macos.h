#pragma once

#include "srtc/event_loop.h"

namespace srtc
{

class EventLoop_MacOS : public EventLoop
{
public:
    EventLoop_MacOS();
    ~EventLoop_MacOS() override;

    void registerSocket(const std::shared_ptr<Socket>& socket, void* udata) override;
    void unregisterSocket(const std::shared_ptr<Socket>& socket) override;

    void wait(std::vector<void*>& udataList, int timeoutMillis) override;

    void interrupt() override;

private:
    int mKQueue;
    int mPipeRead;
    int mPipeWrite;
};

} // namespace srtc
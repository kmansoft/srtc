#pragma once

#include "srtc/event_loop.h"

namespace srtc {

class EventLoop_Linux : public EventLoop {
public:
    EventLoop_Linux();
    ~EventLoop_Linux() override;

    void registerSocket(int socket, void* udata) override;
    void unregisterSocket(int socket) override;

    void wait(std::vector<void*>& udataList,
              int timeoutMillis) override;

    void interrupt() override;

private:
    int mEventHandle;
    int mEpollHandle;
};

};
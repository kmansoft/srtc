#pragma once

#include "srtc/event_loop.h"

#include <mach/port.h>

namespace srtc {

class EventLoop_MacOS : public EventLoop {
public:
    EventLoop_MacOS();
    ~EventLoop_MacOS() override;

    void registerSocket(int socket, void* udata) override;
    void unregisterSocket(int socket) override;

    void wait(std::vector<void*>& udataList,
            int timeoutMillis) override;

    void interrupt() override;

private:
    int mKQueue;
    mach_port_name_t mEventPort;
};

}
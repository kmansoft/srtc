#pragma once

#include <vector>
#include <memory>

namespace srtc {

class EventLoop {
public:
    virtual ~EventLoop() = default;

    virtual void registerSocket(int socket, void* udata) = 0;
    virtual void unregisterSocket(int socket) = 0;

    virtual void wait(std::vector<void*>& udataList,
                      int timeoutMillis) = 0;

    virtual void interrupt() = 0;

    static std::shared_ptr<EventLoop> factory();
};

}
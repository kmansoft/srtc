#pragma once

#include "srtc/srtc.h"

#include <memory>
#include <vector>

namespace srtc
{

class Socket;

class EventLoop
{
public:
    virtual ~EventLoop() = default;

    virtual void registerSocket(const std::shared_ptr<Socket>& socket, void* udata) = 0;
    virtual void unregisterSocket(const std::shared_ptr<Socket>& socket) = 0;

    virtual void wait(std::vector<void*>& udataList, int timeoutMillis) = 0;

    virtual void interrupt() = 0;

    static std::shared_ptr<EventLoop> factory();
};

} // namespace srtc
#include "srtc/event_loop_linux.h"
#include "srtc/logging.h"
#include "srtc/socket.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>

#define LOG(level, ...) srtc::log(level, "EventLoop_Linux", __VA_ARGS__)

namespace srtc
{

std::shared_ptr<EventLoop> EventLoop::factory()
{
    return std::make_shared<EventLoop_Linux>();
}

EventLoop_Linux::EventLoop_Linux()
    : mEventHandle(eventfd(0, EFD_NONBLOCK))
    , mEpollHandle(epoll_create(1))
{
    struct epoll_event ev = {};

    ev.events = EPOLLIN;
    ev.data.ptr = nullptr;

    epoll_ctl(mEpollHandle, EPOLL_CTL_ADD, mEventHandle, &ev);
}

EventLoop_Linux::~EventLoop_Linux()
{
    close(mEpollHandle);
    close(mEventHandle);
}

void EventLoop_Linux::registerSocket(const std::shared_ptr<Socket>& socket, void* udata)
{
    struct epoll_event ev = {};
    ev.events = EPOLLIN;
    ev.data.ptr = udata;
    epoll_ctl(mEpollHandle, EPOLL_CTL_ADD, socket->handle(), &ev);
}

void EventLoop_Linux::unregisterSocket(const std::shared_ptr<Socket>& socket)
{
    epoll_ctl(mEpollHandle, EPOLL_CTL_DEL, socket->handle(), nullptr);
}

void EventLoop_Linux::wait(std::vector<void*>& udataList, int timeoutMillis)
{
    udataList.clear();

    // Calling epoll with timeout < 0 causes it to wait indefinitely
    const auto timeoutArg = std::clamp(timeoutMillis, 0, 100);

    struct epoll_event epollEvent[10];
    const auto nfds = epoll_wait(mEpollHandle, epollEvent, sizeof(epollEvent) / sizeof(epollEvent[0]), timeoutArg);

    if (nfds > 0) {
        for (int i = 0; i < nfds; i += 1) {
            const auto& event = epollEvent[i];
            if (event.data.ptr == nullptr) {
                // The event fd
                eventfd_t value = { 0 };
                eventfd_read(mEventHandle, &value);
            } else {
                // A socket
                udataList.push_back(event.data.ptr);
            }
        }
    } else if (nfds == -1) {
        const auto message = strerror(errno);
        LOG(SRTC_LOG_E, "Error calling epoll_wait: %s", message);
    }
}

void EventLoop_Linux::interrupt()
{
    eventfd_write(mEventHandle, 1);
}

} // namespace srtc
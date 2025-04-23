#include "srtc/event_loop_linux.h"
#include "srtc/logging.h"

#include <chrono>

#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <unistd.h>

#define LOG(level, ...) srtc::log(level, "EventLoop_Linux", __VA_ARGS__)

namespace srtc {

std::shared_ptr<EventLoop> EventLoop::factory()
{
    return std::make_shared<EventLoop_Linux>();
}

EventLoop_Linux::EventLoop_Linux()
    : mEventHandle(eventfd(0, EFD_NONBLOCK))
    , mEpollHandle(epoll_create(1))
{
    struct epoll_event ev = {  };

    ev.events = EPOLLIN;
    ev.data.ptr = nullptr;

    epoll_ctl(mEpollHandle, EPOLL_CTL_ADD, mEventHandle, &ev);
}

EventLoop_Linux::~EventLoop_Linux()
{
    close(mEpollHandle);
    close(mEventHandle);
}

void EventLoop_Linux::registerSocket(int socket, void* udata)
{
    struct epoll_event ev = { };
    ev.events = EPOLLIN;
    ev.data.ptr = udata;
    epoll_ctl(mEpollHandle, EPOLL_CTL_ADD, socket, &ev);
}

void EventLoop_Linux::unregisterSocket(int socket)
{
    epoll_ctl(mEpollHandle, EPOLL_CTL_DEL, socket, nullptr);
}

void EventLoop_Linux::wait(std::vector<void*>& udataList,
                           int timeoutMillis)
{
    udataList.clear();

    struct epoll_event epollEvent[10];
    const auto nfds = epoll_wait(mEpollHandle, epollEvent,
                                 sizeof(epollEvent) / sizeof(epollEvent[0]),
                                 timeoutMillis);

    if (nfds > 0) {
        for (int i = 0; i < nfds; i += 1) {
            const auto& event = epollEvent[i];
            if (event.data.ptr == nullptr) {
                // The event fd
                eventfd_t value = {0};
                eventfd_read(mEventHandle, &value);
            } else {
                // A socket
                udataList.push_back(event.data.ptr);
            }
        }
    }
}

void EventLoop_Linux::interrupt()
{
    eventfd_write(mEventHandle, 1);
}

}
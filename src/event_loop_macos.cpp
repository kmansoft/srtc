#include "srtc/event_loop_macos.h"
#include "srtc/logging.h"
#include "srtc/socket.h"

#include <cstring>
#include <cassert>
#include <unistd.h>
#include <sys/event.h>
#include <sys/types.h>

#define LOG(level, ...) srtc::log(level, "EventLoop_MacOS", __VA_ARGS__)

namespace
{

constexpr auto kInterruptId = 1;

}

namespace srtc
{

std::shared_ptr<EventLoop> EventLoop::factory()
{
    return std::make_shared<EventLoop_MacOS>();
}

EventLoop_MacOS::EventLoop_MacOS()
    : mKQueue(kqueue())
{
    struct kevent change = {};
    EV_SET(&change, kInterruptId, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);

    if (kevent(mKQueue, &change, 1, nullptr, 0, nullptr) == -1) {
        LOG(SRTC_LOG_E, "Cannot add user event to kqueue");
    }
}

EventLoop_MacOS::~EventLoop_MacOS()
{
    close(mKQueue);
}

void EventLoop_MacOS::registerSocket(const std::shared_ptr<Socket>& socket, void* udata)
{
    assert(udata != nullptr);

    struct kevent change = {};
    EV_SET(&change, socket->handle(), EVFILT_READ, EV_ADD, 0, 0, udata);

    if (kevent(mKQueue, &change, 1, nullptr, 0, nullptr) == -1) {
        LOG(SRTC_LOG_E, "Error adding socket to kqueue: %s", strerror(errno));
    }
}

void EventLoop_MacOS::unregisterSocket(const std::shared_ptr<Socket>& socket)
{
    struct kevent change = {};
    EV_SET(&change, socket->handle(), EVFILT_READ, EV_DELETE, 0, 0, nullptr);

    if (kevent(mKQueue, &change, 1, nullptr, 0, nullptr) == -1) {
        LOG(SRTC_LOG_E, "Error removing socket from kqueue: %s", strerror(errno));
    }
}

void EventLoop_MacOS::wait(std::vector<void*>& udataList, int timeoutMillis)
{
    udataList.clear();

    struct kevent event[10];

    struct timespec timeout = {};
    if (timeoutMillis < 0) {
        timeout.tv_sec = 0;
        timeout.tv_nsec = 0;
    } else if (timeoutMillis < 1000) {
        timeout.tv_sec = 0;
        timeout.tv_nsec = timeoutMillis * 1000000;
    } else {
        timeout.tv_sec = timeoutMillis / 1000;
        timeout.tv_nsec = (timeoutMillis % 1000) * 1000000;
    }

    const auto n = kevent(mKQueue, nullptr, 0, event, sizeof(event) / sizeof(event[0]), &timeout);
    if (n > 0) {
        for (int i = 0; i < n; i += 1) {
            const auto& ev = event[i];
            if (ev.udata == nullptr) {
                // Our interrupt event - nothing to drain, EV_CLEAR handles reset
            } else {
                udataList.push_back(ev.udata);
            }
        }
    } else if (n == -1) {
        LOG(SRTC_LOG_E, "Error calling kevent: %s", strerror(errno));
    }
}

void EventLoop_MacOS::interrupt()
{
    struct kevent change = {};
    EV_SET(&change, kInterruptId, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);

    if (kevent(mKQueue, &change, 1, nullptr, 0, nullptr) == -1) {
        LOG(SRTC_LOG_E, "Error triggering user event: %s", strerror(errno));
    }
}

} // namespace srtc
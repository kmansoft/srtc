#include "srtc/event_loop_macos.h"
#include "srtc/logging.h"
#include "srtc/socket.h"

#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>

#define LOG(level, ...) srtc::log(level, "EventLoop_MacOS", __VA_ARGS__)

namespace srtc
{

std::shared_ptr<EventLoop> EventLoop::factory()
{
    return std::make_shared<EventLoop_MacOS>();
}

EventLoop_MacOS::EventLoop_MacOS()
    : mKQueue(kqueue())
    , mPipeRead(-1)
    , mPipeWrite(-1)
{
    int fd[2];
    if (pipe(fd) == -1) {
        LOG(SRTC_LOG_E, "Cannot create a pipe");
    } else {
        mPipeRead = fd[0];
        mPipeWrite = fd[1];
    }

    struct kevent change;
    EV_SET(&change, mPipeRead, EVFILT_READ, EV_ADD, 0, 0, nullptr);

    if (kevent(mKQueue, &change, 1, nullptr, 0, nullptr) == -1) {
        LOG(SRTC_LOG_E, "Cannot add pipe to kqueue");
    }
}

EventLoop_MacOS::~EventLoop_MacOS()
{
    close(mKQueue);
    close(mPipeRead);
    close(mPipeWrite);
}

void EventLoop_MacOS::registerSocket(const std::shared_ptr<Socket>& socket, void* udata)
{
    struct kevent change = {};
    EV_SET(&change, socket->handle(), EVFILT_READ, EV_ADD, 0, 0, udata);

    if (kevent(mKQueue, &change, 1, nullptr, 0, nullptr) == -1) {
        LOG(SRTC_LOG_E, "Cannot add socket to kqueue");
    }
}

void EventLoop_MacOS::unregisterSocket(const std::shared_ptr<Socket>& socket)
{
    struct kevent change = {};
    EV_SET(&change, socket->handle(), EVFILT_READ, EV_DELETE, 0, 0, nullptr);

    if (kevent(mKQueue, &change, 1, nullptr, 0, nullptr) == -1) {
        LOG(SRTC_LOG_E, "Cannot remove socket from kqueue");
    }
}

void EventLoop_MacOS::wait(std::vector<void*>& udataList, int timeoutMillis)
{
    udataList.clear();

    struct kevent event[10];

    struct timespec timeout = {};
    if (timeoutMillis < 1000) {
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
                // Our interrupt event
                uint8_t value;
                read(mPipeRead, &value, sizeof(value));
            } else {
                udataList.push_back(ev.udata);
            }
        }
    }
}

void EventLoop_MacOS::interrupt()
{
    uint8_t value = 0;
    if (write(mPipeWrite, &value, sizeof(value)) != 1) {
        LOG(SRTC_LOG_E, "Cannot write to pipe");
    }
}

} // namespace srtc
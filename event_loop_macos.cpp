#include "srtc/event_loop_macos.h"

#include "srtc/logging.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <mach/mach.h>
#include <mach/port.h>

#define LOG(level, ...) srtc::log(level, "EventLoop_MacOS", __VA_ARGS__)

namespace srtc {

std::shared_ptr<EventLoop> EventLoop::factory()
{
    return std::make_shared<EventLoop_MacOS>();
}
    
EventLoop_MacOS::EventLoop_MacOS()
    : mKQueue(kqueue())
    , mEventPort(0)
{
    if (mach_port_allocate(mach_task_self(), 
            MACH_PORT_RIGHT_RECEIVE, 
            &mEventPort) != KERN_SUCCESS) {
        LOG(SRTC_LOG_E, "Cannot create a mach port");
    }
    if (mach_port_insert_right(mach_task_self(), 
            mEventPort, mEventPort, 
        MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) {
        LOG(SRTC_LOG_E, "Cannot grant rights to mach port");
    }

    struct kevent change;
    EV_SET(&change, mEventPort, EVFILT_MACHPORT, EV_ADD, 0, 0, nullptr);

    if (kevent(mKQueue, &change, 1, nullptr, 0, nullptr) == -1) {
        LOG(SRTC_LOG_E, "Cannot add mach port to kqueue");
    }
}

EventLoop_MacOS::~EventLoop_MacOS()
{
    close(mKQueue);
    if (mach_port_destruct(mach_task_self(), mEventPort, -1, 0) != KERN_SUCCESS) {
        LOG(SRTC_LOG_E, "Cannot destruct a mach port");
    }
    if (mach_port_deallocate(mach_task_self(), mEventPort) != KERN_SUCCESS) {
        LOG(SRTC_LOG_E, "Cannot deallocate a mach port");
    }
}

void EventLoop_MacOS::registerSocket(int socket, void* udata)
{
    struct kevent change = {};
    EV_SET(&change, socket, EVFILT_READ, EV_ADD, 0, 0, udata);
    
    if (kevent(mKQueue, &change, 1, nullptr, 0, nullptr) == -1) {
        LOG(SRTC_LOG_E, "Cannot add socket to kqueue");
    }
}

void EventLoop_MacOS::unregisterSocket(int socket)
{
    struct kevent change = {};
    EV_SET(&change, socket, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    
    if (kevent(mKQueue, &change, 1, nullptr, 0, nullptr) == -1) {
        LOG(SRTC_LOG_E, "Cannot remove socket from kqueue");
    }
}

void EventLoop_MacOS::wait(std::vector<void*>& udataList,
    int timeoutMillis)
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
                // Our mach event
                mach_msg_header_t msg;
                if (mach_msg(
                    &msg,
                    MACH_RCV_MSG,
                    0,
                    sizeof(msg),
                    mEventPort,
                    MACH_MSG_TIMEOUT_NONE,
                    MACH_PORT_NULL) != MACH_MSG_SUCCESS) {
                        LOG(SRTC_LOG_E, "Cannot read mach port message");
                    }
            } else {
                udataList.push_back(ev.udata);
            }
        }
    }
}

void EventLoop_MacOS::interrupt()
{
    mach_msg_header_t header = {};

    header.msgh_bits = MACH_MSGH_BITS_REMOTE(MACH_MSG_TYPE_COPY_SEND);
    header.msgh_size = sizeof(header);
    header.msgh_remote_port = mEventPort;
    header.msgh_local_port = MACH_PORT_NULL;
    header.msgh_id = 1234;

    if (mach_msg(
        &header,
        MACH_SEND_MSG,
        header.msgh_size,
        0,
        MACH_PORT_NULL,
        MACH_MSG_TIMEOUT_NONE,
        MACH_PORT_NULL) != MACH_MSG_SUCCESS) {
            LOG(SRTC_LOG_E, "Cannot send mach port message");
        }
}

}
#include "srtc/event_loop_win.h"
#include "srtc/logging.h"
#include "srtc/socket.h"

#include <cassert>
#include <algorithm>

#define LOG(level, ...) srtc::log(level, "EventLoop_Win", __VA_ARGS__)

namespace srtc
{

std::shared_ptr<EventLoop> EventLoop::factory()
{
    return std::make_shared<EventLoop_Win>();
}

EventLoop_Win::EventLoop_Win()
    : mEventHandle(CreateEvent(NULL, FALSE, FALSE, NULL))
{

}
EventLoop_Win::~EventLoop_Win()
{
    if (mEventHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(mEventHandle);
    }
}

void EventLoop_Win::registerSocket(const std::shared_ptr<Socket>& socket, void* udata)
{
    mSocketList.emplace_back(socket, udata);
}

void EventLoop_Win::unregisterSocket(const std::shared_ptr<Socket>& socket)
{
    for (auto iter = mSocketList.begin(); iter != mSocketList.end(); )
    {
        if (iter->socket.lock() == socket) {
            mSocketList.erase(iter);
            return;
        }

        ++iter;
    }
}

void EventLoop_Win::wait(std::vector<void*>& udataList, int timeoutMillis)
{
    const auto size = 1 + mSocketList.size();

    const auto handleListPtr = mHandleList.ensure(size);
    const auto udataListPtr = mUDataList.ensure(size);

    size_t count = 0;
    handleListPtr[count] = mEventHandle;
    udataListPtr[count] = nullptr;
    count += 1;
    for (const auto& item : mSocketList) {
        const auto socket = item.socket.lock();
        if (socket) {
           handleListPtr[count] = socket->event();
           udataListPtr[count] = item.udata;
           count += 1;
        }
    }

    udataList.clear();

    auto timeoutArg = std::clamp(timeoutMillis, 0, 100);

    const auto res = WaitForMultipleObjects(count, handleListPtr, FALSE, timeoutArg);
    if (res != WAIT_TIMEOUT) {
        const auto index = res - WAIT_OBJECT_0;
        if (index >= 1 && index < count) {
            const auto udata = udataListPtr[index];

            for (const auto& item : mSocketList) {
                const auto socket = item.socket.lock();
                if (socket && item.udata == udata) {
                    assert(socket->event() == handleListPtr[index]);

                    WSANETWORKEVENTS networkEvents;
                    WSAEnumNetworkEvents(socket->handle(), socket->event(), &networkEvents);
                    break;
                }
            }

            udataList.push_back(udata);
        }
    }
}

void EventLoop_Win::interrupt()
{
    SetEvent(mEventHandle);
}

}

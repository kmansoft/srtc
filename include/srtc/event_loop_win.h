#pragma once

#include "srtc/event_loop.h"
#include "srtc/temp_buffer.h"

#include <list>

namespace srtc
{

class EventLoop_Win : public EventLoop
{
public:
    EventLoop_Win();
    ~EventLoop_Win() override;

    void registerSocket(const std::shared_ptr<Socket>& socket, void* udata) override;
    void unregisterSocket(const std::shared_ptr<Socket>& socket) override;

    void wait(std::vector<void*>& udataList, int timeoutMillis) override;

    void interrupt() override;

private:
    const HANDLE mEventHandle;
    
    struct Item
    {
        const std::weak_ptr<Socket> socket;
        void* const udata;

        Item(const std::weak_ptr<Socket>& socket, void* udata) : socket(socket), udata(udata) {}
    };
    std::list<Item> mSocketList;
    FixedTempBuffer<HANDLE> mHandleList;
    FixedTempBuffer<void*> mUDataList;
};

}; // namespace srtc
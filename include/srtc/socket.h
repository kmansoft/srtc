#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/srtc.h"

#include <list>
#include <memory>
#include <string>
#include <cstddef>

namespace srtc
{

class Socket
{
public:
    explicit Socket(const anyaddr& addr);
    ~Socket();

    [[nodiscard]] SocketHandle handle() const;

#ifdef _WIN32
    [[nodiscard]] HANDLE event() const;
#endif

    struct ReceivedData {
        ByteBuffer buf;
        anyaddr addr;
        socklen_t addr_len;

        ReceivedData(ByteBuffer&& buf, const anyaddr& addr, socklen_t addr_len)
            : buf(std::move(buf))
            , addr(addr)
            , addr_len(addr_len)
        {
        }
    };

    [[nodiscard]] std::list<ReceivedData> receive();

    [[nodiscard]] ssize_t send(const ByteBuffer& buf);
    [[nodiscard]] ssize_t send(const void* ptr, size_t len);

private:
    const anyaddr mAddr;
    const SocketHandle mHandle;
#ifdef _WIN32
    const HANDLE mEvent;
#endif
    const std::unique_ptr<uint8_t[]> mReceiveBuffer;
};

} // namespace srtc

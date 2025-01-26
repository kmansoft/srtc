#pragma once

#include "srtc/srtc.h"
#include "srtc/byte_buffer.h"

#include <list>
#include <string>
#include <memory>

namespace srtc {

class Socket {
public:
    explicit Socket(const anyaddr& addr);
    ~Socket();

    [[nodiscard]] int fd() const;

    struct ReceivedData {
        ByteBuffer buf;
        anyaddr addr;
        socklen_t addr_len;

        ReceivedData(ByteBuffer&& buf, const anyaddr& addr, socklen_t addr_len)
            : buf(std::move(buf))
            , addr(addr)
            , addr_len(addr_len) {}
    };

    [[nodiscard]] std::list<ReceivedData> receive();

    [[nodiscard]] ssize_t send(const ByteBuffer& buf);
    [[nodiscard]] ssize_t send(const void* ptr, size_t len);

private:
    const anyaddr mAddr;
    int mFd;
    const std::unique_ptr<uint8_t[]> mReceiveBuffer;
};

}

#include "srtc/socket.h"
#include "srtc/logging.h"
#include "srtc/util.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <cstring>

#define LOG(level, ...) srtc::log(level, "Socket", __VA_ARGS__)

namespace
{

int createSocket(const srtc::anyaddr& addr)
{
    if (addr.ss.ss_family == AF_INET6) {
        return socket(AF_INET6, SOCK_DGRAM, 0);
    }

    return socket(AF_INET, SOCK_DGRAM, 0);
}

constexpr auto kReceiveBufferSize = 16 * 1024;

} // namespace

namespace srtc
{

Socket::Socket(const anyaddr& addr)
    : mAddr(addr)
    , mFd(createSocket(addr))
    , mReceiveBuffer(std::make_unique<uint8_t[]>(kReceiveBufferSize))
{
#ifdef _WIN32
    u_long mode = 1u;
    ioctlsocket(mFd, FIONBIO, &mode);
#else
    auto socketFlags = fcntl(mFd, F_GETFL, 0);
    socketFlags |= O_NONBLOCK;
    fcntl(mFd, F_SETFL, socketFlags);
#endif
}

Socket::~Socket()
{
    if (mFd >= 0) {
#ifdef _WIN32
        closesocket(mFd);
#else
        close(mFd);
#endif
    }
}

int Socket::fd() const
{
    return mFd;
}

[[nodiscard]] std::list<Socket::ReceivedData> Socket::receive()
{
    std::list<ReceivedData> list;

    while (true) {
        union anyaddr from = {};
        socklen_t fromLen = sizeof(from);

        const auto r = recvfrom(
            mFd,
#ifdef _WIN32
            reinterpret_cast<char*>(mReceiveBuffer.get()),
#else
            mReceiveBuffer.get(),
#endif
            kReceiveBufferSize, 0, reinterpret_cast<struct sockaddr*>(&from), &fromLen);
        if (r > 0) {
            if (mAddr == from) {
                ByteBuffer buf = { mReceiveBuffer.get(), static_cast<size_t>(r) };
                list.emplace_back(std::move(buf), from, fromLen);
            }
        } else {
            break;
        }
    }

    return list;
}

ssize_t Socket::send(const ByteBuffer& buf)
{
    return send(buf.data(), buf.size());
}

ssize_t Socket::send(const void* ptr, size_t len)
{
    const auto r = ::sendto(mFd,
#ifdef _WIN32
                            reinterpret_cast<const char*>(ptr),
#else
                            ptr,
#endif
                            len,
                            0,
                            (struct sockaddr*)&mAddr,
                            mAddr.ss.ss_family == AF_INET ? sizeof(mAddr.sin_ipv4) : sizeof(mAddr.sin_ipv6));

    if (r == -1) {
        const auto errorMessage = strerror(errno);
        LOG(SRTC_LOG_E, "Cannot send on a socket: %s", errorMessage);
    }

    return r;
}

} // namespace srtc

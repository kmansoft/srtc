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

#ifdef _WIN32
std::once_flag gWSAStartup;

void initWinSocket()
{
    std::call_once(gWSAStartup, [] {
        WSADATA wsa;
        (void)WSAStartup(MAKEWORD(2, 2), &wsa);
    });
}
#endif

srtc::SocketHandle createSocket(const srtc::anyaddr& addr)
{
#ifdef _WIN32
    initWinSocket();
#endif

    if (addr.ss.ss_family == AF_INET6) {
        return socket(AF_INET6, SOCK_DGRAM, 0);
    }

    return socket(AF_INET, SOCK_DGRAM, 0);
}

#ifdef _WIN32
HANDLE createEvent(SOCKET socket)
{
    const auto event = WSACreateEvent();

    WSAEventSelect(socket, event, FD_READ);

    return event;
}
#endif

constexpr auto kReceiveBufferSize = 16 * 1024;

} // namespace

namespace srtc
{

Socket::Socket(const anyaddr& addr)
    : mAddr(addr)
    , mHandle(createSocket(addr))
#ifdef _WIN32
    , mEvent(createEvent(mHandle))
#endif
    , mReceiveBuffer(std::make_unique<uint8_t[]>(kReceiveBufferSize))
{
#ifdef _WIN32
    u_long mode = 1u;
    ioctlsocket(mHandle, FIONBIO, &mode);
#else
    auto socketFlags = fcntl(mHandle, F_GETFL, 0);
    socketFlags |= O_NONBLOCK;
    fcntl(mHandle, F_SETFL, socketFlags);
#endif
}

Socket::~Socket()
{
#ifdef _WIN32
    if (mHandle != INVALID_SOCKET) {
        closesocket(mHandle);
    }
    if (mEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(mEvent);
    }
#else
    if (mHandle >= 0) {
        close(mHandle);
    }
#endif
}

SocketHandle Socket::handle() const
{
    return mHandle;
}

#ifdef _WIN32
HANDLE Socket::event() const
{
    return mEvent;
}
#endif

[[nodiscard]] std::list<Socket::ReceivedData> Socket::receive()
{
    std::list<ReceivedData> list;

    while (true) {
        union anyaddr from = {};
        socklen_t fromLen = sizeof(from);

        const auto r = recvfrom(mHandle,
#ifdef _WIN32
                                reinterpret_cast<char*>(mReceiveBuffer.get()),
#else
                                mReceiveBuffer.get(),
#endif
                                kReceiveBufferSize,
                                0,
                                reinterpret_cast<struct sockaddr*>(&from),
                                &fromLen);
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
    const auto r = sendto(mHandle,
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
#ifdef _WIN32
        const auto error = GetLastError();
        char message[1024];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL,
                       error,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       message,
                       sizeof(message),
                       NULL);
        LOG(SRTC_LOG_E, "Cannot send on a socket: %s", message);
#else
        const auto e = errno;
        if (e != EINTR && e != EAGAIN) {
            const auto message = strerror(e);
            LOG(SRTC_LOG_E, "Cannot send on a socket: %s", message);
        }
#endif
    }

    return r;
}

} // namespace srtc

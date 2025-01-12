#include "srtc/socket.h"

namespace {

int createSocket(const srtc::anyaddr &addr)
{
    if (addr.ss.ss_family == AF_INET6) {
        return socket(AF_INET6, SOCK_DGRAM, 0);
    }

    return socket(AF_INET, SOCK_DGRAM, 0);
}

bool operator==(
        const struct sockaddr_in &sin1,
        const struct sockaddr_in &sin2)
{
    return sin1.sin_family == sin2.sin_family &&
           sin1.sin_port == sin2.sin_port &&
           sin1.sin_addr.s_addr == sin2.sin_addr.s_addr;
}

bool operator==(
        const struct sockaddr_in6 &sin1,
        const struct sockaddr_in6 &sin2)
{
    return sin1.sin6_family == sin2.sin6_family &&
           sin1.sin6_port == sin2.sin6_port &&
           std::memcmp(&sin1.sin6_addr, &sin2.sin6_addr, sizeof(sin1.sin6_addr)) == 0;
}

constexpr auto kReceiveBufferSize = 2048;

}

namespace srtc {

Socket::Socket(const anyaddr& addr)
    : mAddr(addr)
    , mFd(createSocket(addr))
    , mReceiveBuffer(std::make_unique<uint8_t[]>(kReceiveBufferSize))
{
    auto socketFlags = fcntl(mFd, F_GETFL, 0);
    socketFlags |= O_NONBLOCK;
    fcntl(mFd, F_SETFL, socketFlags);
}

Socket::~Socket()
{
    if (mFd >= 0) {
        close(mFd);
        mFd = -1;
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

        const auto r = recvfrom(mFd, mReceiveBuffer.get(), kReceiveBufferSize, 0,
                                reinterpret_cast<struct sockaddr *>(&from),
                                &fromLen);
        if (r > 0) {
            if (mAddr.ss.ss_family == AF_INET && mAddr.sin_ipv4 == from.sin_ipv4 ||
                mAddr.ss.ss_family == AF_INET6 && mAddr.sin_ipv6 == from.sin_ipv6) {
                list.emplace_back(ByteBuffer(mReceiveBuffer.get(), r), from, fromLen);
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
    return ::sendto(mFd, ptr, len, 0, (struct sockaddr *) &mAddr, sizeof(mAddr));
}

}

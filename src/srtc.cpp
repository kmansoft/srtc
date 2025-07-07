#include "srtc/srtc.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <string>

namespace srtc
{

std::string to_string(MediaType m)
{
	switch (m) {
	case MediaType::Video:
		return "video";
	case MediaType::Audio:
		return "audio";
	default:
		return "unknown-" + std::to_string(static_cast<unsigned int>(m));
	}
}

std::string to_string(const anyaddr& addr)
{
    char buf[INET6_ADDRSTRLEN + 16];
    const char* ptr;
    if (addr.ss.ss_family == AF_INET6) {
        ptr = inet_ntop(addr.sin_ipv6.sin6_family, &addr.sin_ipv6.sin6_addr, buf, sizeof(buf));
    } else {
        ptr = inet_ntop(addr.sin_ipv4.sin_family, &addr.sin_ipv4.sin_addr, buf, sizeof(buf));
    }

    if (ptr == nullptr) {
        return "???";
    }

    std::string addrs = { ptr };

    if (addr.ss.ss_family == AF_INET6) {
        addrs += ":";
        addrs += std::to_string(ntohs(addr.sin_ipv6.sin6_port));
    } else {
        addrs += ":";
        addrs += std::to_string(ntohs(addr.sin_ipv4.sin_port));
    }

    return addrs;
}

} // namespace srtc

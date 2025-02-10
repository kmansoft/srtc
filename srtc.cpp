#include "srtc/srtc.h"

#include <arpa/inet.h>

#include <string>

namespace srtc {

std::string to_string(const anyaddr& addr)
{
    char buf[INET6_ADDRSTRLEN + 16];
    const char* ptr;
    if (addr.ss.ss_family == AF_INET6) {
        ptr = inet_ntop(addr.sin_ipv6.sin6_family,
                        &addr.sin_ipv6.sin6_addr, buf, sizeof(buf));
    } else {
        ptr = inet_ntop(addr.sin_ipv4.sin_family,
                  &addr.sin_ipv4.sin_addr, buf, sizeof(buf));
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

}

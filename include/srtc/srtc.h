#pragma once

#include <arpa/inet.h>

namespace srtc {

enum class Codec {
    Unknown = 0,
    H264 = 1,
    Opus = 100
};

struct Host {
    int family;
    union {
        struct in_addr ipv4;
        struct in6_addr ipv6;
    } host;
    int port;
};

#define SRTC_GUARDED_BY(mutex) __attribute__((guarded_by(mutex)))

}

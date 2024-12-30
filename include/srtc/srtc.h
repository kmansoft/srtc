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

union anyaddr {
    struct sockaddr_storage ss;
    struct sockaddr_in sin_ipv4;
    struct sockaddr_in6 sin_ipv6;
};

#define SRTC_GUARDED_BY(mutex) __attribute__((guarded_by(mutex)))
#define SRTC_LOCKS_EXCLUDED(...) __attribute__((locks_excluded(__VA_ARGS__)))
#define SRTC_EXCLUSIVE_LOCKS_REQUIRED(...) __attribute__((exclusive_locks_required(__VA_ARGS__)))
#define SRTC_SHARED_LOCKS_REQUIRED(...) __attribute__((shared_locks_required(__VA_ARGS__)))

}

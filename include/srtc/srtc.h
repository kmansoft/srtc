#pragma once

#include <arpa/inet.h>
#include <thread>

namespace srtc {

enum class Codec {
    None = 0,
    H264 = 1,
    Opus = 100,
    Rtx = 200
};

enum class MediaType {
    None = 0,
    Video = 1,
    Audio = 2
};

union anyaddr {
    struct sockaddr_storage ss;
    struct sockaddr_in sin_ipv4;
    struct sockaddr_in6 sin_ipv6;
};

struct Host {
    union anyaddr addr;
};

std::string to_string(const anyaddr& addr);

#if defined __has_attribute && __has_attribute(guarded_by)
#define SRTC_GUARDED_BY(mutex) __attribute__((guarded_by(mutex)))
#else
#define SRTC_GUARDED_BY(mutex)
#endif

#if defined __has_attribute && __has_attribute(locks_excluded)
#define SRTC_LOCKS_EXCLUDED(...) __attribute__((locks_excluded(__VA_ARGS__)))
#else
#define SRTC_LOCKS_EXCLUDED(...)
#endif

#if defined __has_attribute && __has_attribute(exclusive_locks_required)
#define SRTC_EXCLUSIVE_LOCKS_REQUIRED(...) __attribute__((exclusive_locks_required(__VA_ARGS__)))
#else
#define SRTC_EXCLUSIVE_LOCKS_REQUIRED(...)
#endif

#if defined __has_attribute && __has_attribute(shared_locks_required)
#define SRTC_SHARED_LOCKS_REQUIRED(...) __attribute__((shared_locks_required(__VA_ARGS__)))
#else
#define SRTC_SHARED_LOCKS_REQUIRED(...)
#endif

}

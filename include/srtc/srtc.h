#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <string>
#include <thread>
#include <vector>

namespace srtc
{

#ifdef _WIN32
typedef SSIZE_T ssize_t;
typedef SOCKET SocketHandle;
#else
typedef int SocketHandle;
#endif

enum class Codec {
    H264 = 1,
    Opus = 100,
    Rtx = 200
};

enum class MediaType {
    Video = 1,
    Audio = 2
};

std::string to_string(MediaType m);

enum class Direction {
    Publish = 0,
    Subscribe = 1
};

union anyaddr {
    struct sockaddr_storage ss;
    struct sockaddr_in sin_ipv4;
    struct sockaddr_in6 sin_ipv6;
};

std::string to_string(const anyaddr& addr);

struct Host {
    union anyaddr addr;
};

struct PublishConnectionStats {
    size_t packet_count;
    size_t byte_count;
    float packets_lost_percent;
    float rtt_ms;
    float bandwidth_actual_kbit_per_second;
    float bandwidth_suggested_kbit_per_second;
};

enum class PacketKind {
    Standalone = 0,
    Start = 1,
    Middle = 2,
    End = 3
};

#if defined(__clang__) || defined(__GNUC__)

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

#else

#define SRTC_GUARDED_BY(mutex)
#define SRTC_LOCKS_EXCLUDED(...)
#define SRTC_EXCLUSIVE_LOCKS_REQUIRED(...)
#define SRTC_SHARED_LOCKS_REQUIRED(...)

#endif

} // namespace srtc

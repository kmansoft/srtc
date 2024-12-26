#pragma once

#include <arpa/inet.h>

namespace srtc {

enum class VideoCodec {
    Unknown = 0,
    H264 = 1
};

enum class AudioCodec {
    Unknown = 0,
    Opus = 1
};

struct Host {
    int family;
    union {
        struct in_addr ipv4;
        struct in6_addr ipv6;
    } host;
    int port;
};

}

#pragma once

#include "srtc/srtc.h"
#include "srtc/byte_buffer.h"

#include <string>
#include <cstdint>

namespace srtc {

std::string bin_to_hex(const uint8_t* buf,
                       size_t size);

ByteBuffer hex_to_bin(const std::string& hex);

bool operator==(
        const struct sockaddr_in& sin1,
        const struct sockaddr_in& sin2);
bool operator==(
        const struct sockaddr_in6& sin1,
        const struct sockaddr_in6& sin2);
bool operator==(
        const anyaddr& addr1,
        const anyaddr& addr2);

struct NtpTime {
    uint32_t seconds;    // Seconds since Jan 1, 1900
    uint32_t fraction;   // Fraction of second (in 2^-32 seconds)
};

void getNtpTime(NtpTime& ntp);

}

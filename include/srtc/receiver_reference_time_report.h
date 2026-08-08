#pragma once

#include "srtc/srtc.h"
#include "srtc/util.h"

#include <cstdint>
#include <chrono>

namespace srtc
{

struct ReceiverReferenceTimeReport {
    uint32_t ssrc = {};
    std::chrono::steady_clock::time_point when = {};
    NtpTime ntp = {};
};

}
#pragma once

#include "srtc/byte_buffer.h"

#include <vector>
#include <string>

namespace srtc {

struct SimulcastLayer {
    std::string name;
    uint16_t width;
    uint16_t height;
    uint16_t framesPerSecond;
    uint32_t kilobitPerSecond;
};

void buildGoogleVLA(
    ByteBuffer& buf,
    uint8_t ridId,
    const std::vector<std::shared_ptr<SimulcastLayer>>& list);

}
#pragma once

#include "srtc/byte_buffer.h"

#include <memory>
#include <string>
#include <vector>

namespace srtc
{

struct SimulcastLayer {
    std::string name;
    uint16_t width;
    uint16_t height;
    uint16_t frames_per_second;
    uint32_t kilobits_per_second;
};

void buildGoogleVLA(ByteBuffer& buf, uint8_t ridId, const std::vector<std::shared_ptr<SimulcastLayer>>& list);

} // namespace srtc
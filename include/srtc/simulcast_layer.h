#pragma once

#include <string>

namespace srtc {

struct SimulcastLayer {
    std::string name;
    uint16_t width;
    uint16_t height;
    uint16_t framesPerSecond;
    uint32_t kilobitPerSecond;
};


}
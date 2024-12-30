#pragma once

#include "srtc/byte_buffer.h"

#include <string>
#include <cstdint>

namespace srtc {

std::string bin_to_hex(const uint8_t* buf,
                       size_t size);

ByteBuffer hex_to_bin(const std::string& hex);

}

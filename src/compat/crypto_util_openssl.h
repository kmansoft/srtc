#pragma once

#include <cstddef>

namespace srtc::compat
{

// Constant time comparison
bool secureEquals(const void* a, const void* b, size_t size);

// Random bytes
void randomBytes(unsigned char* dst, size_t len);

}

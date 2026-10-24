#pragma once

#include <cstddef>

namespace srtc::compat
{

// Constant time comparison
bool secureEquals(const void* a, const void* b, size_t size);

// Random bytes
void randomBytes(unsigned char* dst, size_t len);

// Zeroes memory holding secrets, unlike memset this is not optimized away when the memory is not read again
void secureZero(void* dst, size_t len);

}

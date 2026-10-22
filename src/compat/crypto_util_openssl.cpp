#include "crypto_util_openssl.h"

#include <openssl/rand.h>
#include <openssl/crypto.h>

#include <cassert>

namespace srtc::compat
{

bool secureEquals(const void* a, const void* b, size_t size)
{
    return CRYPTO_memcmp(a, b, size) == 0;
}

void randomBytes(unsigned char* dst, size_t len)
{
    const auto r = RAND_bytes(dst, static_cast<int>(len));
    assert(r);
    (void) r;
}

}

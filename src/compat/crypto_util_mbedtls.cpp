#include "crypto_util_mbedtls.h"

// Unlike the other mbedTLS headers, constant_time.h has no extern "C" guard (as of 3.6)
extern "C" {
#include <mbedtls/constant_time.h>
}
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/platform_util.h>

#include <algorithm>
#include <cassert>
#include <mutex>

namespace
{

constexpr char kDrbgPersonalization[] = "srtc_random_bytes";

// A process wide random generator, ctr_drbg is not thread safe on its own
struct RandomGenerator {
    std::mutex mutex;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    bool isSeeded = false;

    RandomGenerator()
    {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);

        isSeeded = mbedtls_ctr_drbg_seed(&drbg,
                                         mbedtls_entropy_func,
                                         &entropy,
                                         reinterpret_cast<const unsigned char*>(kDrbgPersonalization),
                                         sizeof(kDrbgPersonalization) - 1) == 0;
    }

    ~RandomGenerator()
    {
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }
};

RandomGenerator& getRandomGenerator()
{
    static RandomGenerator generator;
    return generator;
}

} // namespace

namespace srtc::compat
{

bool secureEquals(const void* a, const void* b, size_t size)
{
    return mbedtls_ct_memcmp(a, b, size) == 0;
}

void randomBytes(unsigned char* dst, size_t len)
{
    auto& generator = getRandomGenerator();
    std::lock_guard lock(generator.mutex);

    assert(generator.isSeeded);

    // ctr_drbg has a per call limit, so generate in chunks
    while (len > 0) {
        const auto chunk = std::min<size_t>(len, MBEDTLS_CTR_DRBG_MAX_REQUEST);
        const auto r = mbedtls_ctr_drbg_random(&generator.drbg, dst, chunk);
        assert(r == 0);
        (void)r;

        dst += chunk;
        len -= chunk;
    }
}

void secureZero(void* dst, size_t len)
{
    mbedtls_platform_zeroize(dst, len);
}

}

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(SRTC_USE_OPENSSL)
#include <openssl/hmac.h>
#elif defined(SRTC_USE_MBEDTLS)
#include <mbedtls/md.h>
#endif

namespace srtc
{

class HmacSha1
{
public:
    HmacSha1();
    ~HmacSha1();

    [[nodiscard]] bool reset(const uint8_t* key, size_t keySize);
    void update(const uint8_t* data, size_t size);
    void final(uint8_t* out);

private:
#if defined(SRTC_USE_OPENSSL)
#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 3
    EVP_MAC* mMac;
    EVP_MAC_CTX* mCtx;
#else
    HMAC_CTX* mCtx;
#endif
#elif defined(SRTC_USE_MBEDTLS)
    mbedtls_md_context_t mCtx;
    bool mIsSetup;
#endif
};

} // namespace srtc

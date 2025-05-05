#pragma once

#include <cstddef>
#include <cstdint>

#include <openssl/hmac.h>

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
#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 3
    EVP_MAC* mMac;
    EVP_MAC_CTX* mCtx;
#else
    HMAC_CTX* mCtx;
#endif
};

} // namespace srtc
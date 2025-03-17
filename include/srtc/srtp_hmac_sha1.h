#pragma once

#include <cstdint>
#include <cstddef>

#include <openssl/hmac.h>

namespace srtc {

class HmacSha1 {
public:
    HmacSha1();
    ~HmacSha1();

    [[nodiscard]] bool reset(const uint8_t* key,
                             size_t keySize);
    void update(const uint8_t* data,
                size_t size);
    void final(uint8_t* out);

private:
    HMAC_CTX* mCtx;
};

}
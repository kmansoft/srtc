// Some code in this file is based on Cisco's libSRTP
// https://github.com/cisco/libsrtp
// Copyright (c) 2001-2017 Cisco Systems, Inc.
// All rights reserved.
// Neither the name of the Cisco Systems, Inc. nor the names of its
// contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.

#include "srtc/srtp_hmac_sha1.h"

#include <cassert>
#include <cstring>

namespace srtc
{

HmacSha1::HmacSha1()
#if defined(SRTC_USE_OPENSSL)
#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 3
    : mMac(EVP_MAC_fetch(NULL, "HMAC", NULL))
    , mCtx(EVP_MAC_CTX_new(mMac))
#else
    : mCtx(HMAC_CTX_new())
#endif
#elif defined(SRTC_USE_MBEDTLS)
    : mCtx()
    , mIsSetup(false)
#endif
{
#if defined(SRTC_USE_MBEDTLS)
    mbedtls_md_init(&mCtx);

    const auto info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mIsSetup = info != nullptr && mbedtls_md_setup(&mCtx, info, 1 /* hmac */) == 0;
#endif
}

HmacSha1::~HmacSha1()
{
#if defined(SRTC_USE_OPENSSL)
#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 3
    if (mCtx) {
        EVP_MAC_CTX_free(mCtx);
    }
    if (mMac) {
        EVP_MAC_free(mMac);
    }
#else
    if (mCtx) {
        HMAC_CTX_free(mCtx);
    }
#endif
#elif defined(SRTC_USE_MBEDTLS)
    mbedtls_md_free(&mCtx);
#endif
}

bool HmacSha1::reset(const uint8_t* key, size_t keySize)
{
#if defined(SRTC_USE_OPENSSL)
    if (!mCtx) {
        return false;
    }

#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 3
    char sha1[12] = { 'S', 'H', 'A', '1' };

    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string("digest", sha1, 0);
    params[1] = OSSL_PARAM_construct_end();

    return EVP_MAC_init(mCtx, key, keySize, params) == 1;
#else
    return HMAC_Init_ex(mCtx, key, static_cast<int>(keySize), EVP_sha1(), nullptr) == 1;
#endif
#elif defined(SRTC_USE_MBEDTLS)
    if (!mIsSetup) {
        return false;
    }

    return mbedtls_md_hmac_starts(&mCtx, key, keySize) == 0;
#endif
}

void HmacSha1::update(const uint8_t* data, size_t size)
{
#if defined(SRTC_USE_OPENSSL)
#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 3
    EVP_MAC_update(mCtx, data, size);
#else
    HMAC_Update(mCtx, data, size);
#endif
#elif defined(SRTC_USE_MBEDTLS)
    mbedtls_md_hmac_update(&mCtx, data, size);
#endif
}

void HmacSha1::final(uint8_t* out)
{
#if defined(SRTC_USE_OPENSSL)
#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 3
    size_t out_len;
    EVP_MAC_final(mCtx, out, &out_len, 20);
#else
    unsigned int out_len;
    HMAC_Final(mCtx, out, &out_len);
#endif

    assert(out_len == 20);
#elif defined(SRTC_USE_MBEDTLS)
    [[maybe_unused]] const auto r = mbedtls_md_hmac_finish(&mCtx, out);
    assert(r == 0);
#endif
}

} // namespace srtc

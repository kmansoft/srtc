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

namespace srtc {

HmacSha1::HmacSha1()
#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x30000000L
    : mMac(EVP_MAC_fetch(NULL, "HMAC", NULL))
    , mCtx(EVP_MAC_CTX_new(mMac))
#else
    : mCtx(HMAC_CTX_new())
#endif
{
}

HmacSha1::~HmacSha1()
{
#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x30000000L
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
}

bool HmacSha1::reset(const uint8_t* key,
                     size_t keySize)
{
    if (!mCtx) {
        return false;
    }

#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x30000000L
    char sha1[12];
    std::strncpy(sha1, "SHA1", sizeof(sha1));

    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string("digest", sha1, 0);
    params[1] = OSSL_PARAM_construct_end();

    EVP_MAC_init(mCtx, key, static_cast<int>(keySize), params);
#else
    HMAC_Init_ex(mCtx, key, static_cast<int>(keySize), EVP_sha1(), nullptr);
#endif
    return true;
}

void HmacSha1::update(const uint8_t* data,
                      size_t size)
{
#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x30000000L
    EVP_MAC_update(mCtx, data, size);
#else
    HMAC_Update(mCtx, data, size);
#endif
}

void HmacSha1::final(uint8_t* out)
{
#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x30000000L
    size_t out_len;
    EVP_MAC_final(mCtx, out, &out_len, 20);
#else
    unsigned int out_len;
    HMAC_Final(mCtx, out, &out_len);
#endif

    assert(out_len == 20);
}

}

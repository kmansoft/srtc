// Some code in this file is based on Cisco's libSRTP
// https://github.com/cisco/libsrtp
// Copyright (c) 2001-2017 Cisco Systems, Inc.
// All rights reserved.
// Neither the name of the Cisco Systems, Inc. nor the names of its
// contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.

#include "srtc/srtp_hmac_sha1.h"

#include <cassert>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace srtc {

HmacSha1::HmacSha1()
    : mCtx(HMAC_CTX_new())
{
}

HmacSha1::~HmacSha1()
{
    if (mCtx) {
        HMAC_CTX_free(mCtx);
    }
}

bool HmacSha1::reset(const uint8_t* key,
                     size_t keySize)
{
    if (!mCtx) {
        return false;
    }

    HMAC_Init_ex(mCtx, key, static_cast<int>(keySize), EVP_sha1(), nullptr);
    return true;
}

void HmacSha1::update(const uint8_t* data,
                      size_t size)
{
    HMAC_Update(mCtx, data, size);
}

void HmacSha1::final(uint8_t* out)
{
    unsigned int out_len;
    HMAC_Final(mCtx, out, &out_len);

    assert(out_len == 20);
}

}

#pragma clang diagnostic pop

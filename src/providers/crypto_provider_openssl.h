#pragma once

#include "srtc/srtp_util.h"

struct evp_cipher_ctx_st;
struct evp_cipher_st;

namespace srtc::providers
{

class CryptoProvider
{
public:
    CryptoProvider(ProfileId profileId);
    ~CryptoProvider();

private:
    const ProfileId mProfileId;
    struct evp_cipher_ctx_st* mCipherCtx;
};

void initCryptoProvider();

}

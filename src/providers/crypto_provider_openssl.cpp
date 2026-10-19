#include "crypto_provider_openssl.h"

#include <mutex>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/srtp.h>
#include <openssl/err.h>

namespace
{

std::once_flag gInitFlag;

}

namespace srtc::providers
{

void initCryptoProvider()
{
    std::call_once(gInitFlag, [] {
        OpenSSL_add_all_algorithms();
        OpenSSL_add_all_ciphers();
        OpenSSL_add_all_digests();
        ERR_load_crypto_strings();
    });
}

CryptoProvider::CryptoProvider(ProfileId profileId) : mProfileId(profileId)
{

}

CryptoProvider::~CryptoProvider()
{
    if (mCipherCtx) {
        mCipherCtx = nullptr;
    }
}

};

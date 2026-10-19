#include "crypto_provider_openssl.h"

#include <mutex>

#include <openssl/err.h>
#include <openssl/evp.h>

namespace
{

std::once_flag gInitFlag;

}

namespace srtc::providers
{

void initCryptoProvider()
{
    std::call_once(gInitFlag, [] {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
        // These are deprecated / removed in 3.x and 4.x
        OpenSSL_add_all_algorithms();
        OpenSSL_add_all_ciphers();
        OpenSSL_add_all_digests();
        ERR_load_crypto_strings();
#endif
    });
}

CryptoProvider::CryptoProvider(ProfileId profileId)
    : mProfileId(profileId)
    , mCipherCtx(nullptr)
{
    const auto cipher = createCipher();
    if (cipher) {
        mCipherCtx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(mCipherCtx, cipher, nullptr, nullptr, nullptr);
    }
}

CryptoProvider::~CryptoProvider()
{
    if (mCipherCtx) {
        EVP_CIPHER_CTX_free(mCipherCtx);
        mCipherCtx = nullptr;
    }
}

CryptoProvider::EncryptContext CryptoProvider::createEncryptContext(const uint8_t* key, const uint8_t* iv)
{
    if (mCipherCtx) {
        if (!EVP_EncryptInit_ex(mCipherCtx, nullptr, nullptr, key, iv)) {
            goto fail;
        }
        return { mCipherCtx };
    }

fail:
    return {};
}

bool CryptoProvider::EncryptContext::update_aad(const uint8_t* data, size_t size) const
{
    int len = 0;
    return EVP_EncryptUpdate(cipher_ctx, nullptr, &len, data, static_cast<int>(size)) != 0;
}

bool CryptoProvider::EncryptContext::update_data(uint8_t* out, const uint8_t* data, size_t size, size_t* out_size) const
{
    int len = 0;

    if (EVP_EncryptUpdate(cipher_ctx, out, &len, data, static_cast<int>(size)) != 0) {
        *out_size = static_cast<size_t>(len);
        return true;
    }

    return false;
}

bool CryptoProvider::EncryptContext::finalize(uint8_t* out, size_t* out_size) const
{
    int len = 0;
    if (EVP_EncryptFinal_ex(cipher_ctx, out, &len) != 0) {
        *out_size = static_cast<size_t>(len);
        return true;
    }

    return false;
}

bool CryptoProvider::EncryptContext::get_digest(uint8_t* out, size_t size) const
{
    return EVP_CIPHER_CTX_ctrl(cipher_ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(size), out) != 0;
}

CryptoProvider::DecryptContext CryptoProvider::createDecryptContext(const uint8_t* key, const uint8_t* iv)
{
    if (mCipherCtx) {
        if (!EVP_DecryptInit_ex(mCipherCtx, nullptr, nullptr, key, iv)) {
            goto fail;
        }
        return { mCipherCtx };
    }

fail:
    return {};
}

const struct evp_cipher_st* CryptoProvider::createCipher() const
{
    switch (mProfileId) {
    case ProfileId::kAES_256_GCM:
        return EVP_aes_256_gcm();
    case ProfileId::kAES_128_GCM:
        return EVP_aes_128_gcm();
    case ProfileId::kAES_128_CM_SHA1_80:
    case ProfileId::kAES_128_CM_SHA1_32:
        return EVP_aes_128_ctr();
    default:
        return nullptr;
    }
}

} // namespace srtc::providers

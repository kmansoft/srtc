#include "crypto_provider_mbedtls.h"
#include "crypto_util.h"

#include <mbedtls/build_info.h>
// Unlike the other mbedTLS headers, constant_time.h has no extern "C" guard (as of 3.6)
extern "C" {
#include <mbedtls/constant_time.h>
}

#if defined(MBEDTLS_PSA_CRYPTO_C)
#include <psa/crypto.h>
#endif

#include <algorithm>
#include <cstring>
#include <mutex>

namespace
{

std::once_flag gInitFlag;

constexpr size_t kGcmIvSize = 12;

bool is_gcm(srtc::ProfileId profileId)
{
    return profileId == srtc::ProfileId::kAES_256_GCM || profileId == srtc::ProfileId::kAES_128_GCM;
}

size_t get_key_size(srtc::ProfileId profileId)
{
    return profileId == srtc::ProfileId::kAES_256_GCM ? 32 : 16;
}

} // namespace

namespace srtc::compat
{

void initCryptoProvider()
{
    std::call_once(gInitFlag, [] {
#if defined(MBEDTLS_PSA_CRYPTO_C)
        // Required by mbedTLS 3.x for TLS 1.3 and when MBEDTLS_USE_PSA_CRYPTO is set, and harmless otherwise
        psa_crypto_init();
#endif
    });
}

CryptoProvider::CryptoProvider(ProfileId profileId)
    : mIsGcm(is_gcm(profileId))
    , mKeySize(get_key_size(profileId))
    , mGcm()
    , mIsAadFlushed(false)
    , mTag()
    , mExpectedTag()
    , mExpectedTagSize(0)
    , mAes()
    , mCtrNonceCounter()
    , mCtrStreamBlock()
    , mCtrOffset(0)
{
    mbedtls_gcm_init(&mGcm);
    mbedtls_aes_init(&mAes);

    mAad.reserve(1500);
}

CryptoProvider::~CryptoProvider()
{
    mbedtls_gcm_free(&mGcm);
    mbedtls_aes_free(&mAes);

    // The keystream block is derived from the key
    secureZero(mCtrNonceCounter, sizeof(mCtrNonceCounter));
    secureZero(mCtrStreamBlock, sizeof(mCtrStreamBlock));
}

bool CryptoProvider::start(const uint8_t* key, const uint8_t* iv, bool encrypt)
{
    const auto keyBits = static_cast<unsigned int>(mKeySize * 8);

    if (mIsGcm) {
        mAad.clear();
        mIsAadFlushed = false;
        mExpectedTagSize = 0;

        if (mbedtls_gcm_setkey(&mGcm, MBEDTLS_CIPHER_ID_AES, key, keyBits) != 0) {
            return false;
        }

        return mbedtls_gcm_starts(&mGcm, encrypt ? MBEDTLS_GCM_ENCRYPT : MBEDTLS_GCM_DECRYPT, iv, kGcmIvSize) == 0;
    }

    // AES-CM is AES in counter mode, which uses the encryption key schedule in both directions
    if (mbedtls_aes_setkey_enc(&mAes, key, keyBits) != 0) {
        return false;
    }

    std::memcpy(mCtrNonceCounter, iv, sizeof(mCtrNonceCounter));
    std::memset(mCtrStreamBlock, 0, sizeof(mCtrStreamBlock));
    mCtrOffset = 0;

    return true;
}

bool CryptoProvider::updateAad(const uint8_t* data, size_t size)
{
    if (!mIsGcm || mIsAadFlushed) {
        // Only GCM has AAD, and it all has to come before the data
        return false;
    }

    mAad.insert(mAad.end(), data, data + size);
    return true;
}

bool CryptoProvider::flushAad()
{
    if (mIsAadFlushed) {
        return true;
    }

    mIsAadFlushed = true;

    if (mAad.empty()) {
        return true;
    }

    return mbedtls_gcm_update_ad(&mGcm, mAad.data(), mAad.size()) == 0;
}

bool CryptoProvider::updateData(uint8_t* out, const uint8_t* data, size_t size, size_t* out_size)
{
    if (mIsGcm) {
        if (!flushAad()) {
            return false;
        }

        size_t len = 0;
        if (mbedtls_gcm_update(&mGcm, data, size, out, size, &len) != 0) {
            return false;
        }

        // The built in implementation outputs as it goes, the callers rely on that
        if (len != size) {
            return false;
        }

        *out_size = len;
        return true;
    }

    if (mbedtls_aes_crypt_ctr(&mAes, size, &mCtrOffset, mCtrNonceCounter, mCtrStreamBlock, data, out) != 0) {
        return false;
    }

    *out_size = size;
    return true;
}

bool CryptoProvider::finish(uint8_t* out, size_t* out_size)
{
    *out_size = 0;

    if (mIsGcm) {
        if (!flushAad()) {
            return false;
        }

        size_t len = 0;
        if (mbedtls_gcm_finish(&mGcm, out, 0, &len, mTag, kGcmTagSize) != 0) {
            return false;
        }

        *out_size = len;
    }

    return true;
}

// Encryption

CryptoProvider::EncryptContext CryptoProvider::createEncryptContext(const uint8_t* key, const uint8_t* iv)
{
    if (!start(key, iv, true)) {
        return { nullptr };
    }

    return { this };
}

bool CryptoProvider::EncryptContext::update_aad(const uint8_t* data, size_t size) const
{
    return provider->updateAad(data, size);
}

bool CryptoProvider::EncryptContext::update_data(uint8_t* out, const uint8_t* data, size_t size, size_t* out_size) const
{
    return provider->updateData(out, data, size, out_size);
}

bool CryptoProvider::EncryptContext::finalize(uint8_t* out, size_t* out_size) const
{
    return provider->finish(out, out_size);
}

bool CryptoProvider::EncryptContext::get_digest(uint8_t* out, size_t size) const
{
    if (!provider->mIsGcm || size > kGcmTagSize) {
        return false;
    }

    std::memcpy(out, provider->mTag, size);
    return true;
}

// Decryption

CryptoProvider::DecryptContext CryptoProvider::createDecryptContext(const uint8_t* key, const uint8_t* iv)
{
    if (!start(key, iv, false)) {
        return { nullptr };
    }

    return { this };
}

bool CryptoProvider::DecryptContext::set_tag(const uint8_t* tag, size_t size) const
{
    if (!provider->mIsGcm || size == 0 || size > kGcmTagSize) {
        return false;
    }

    std::memcpy(provider->mExpectedTag, tag, size);
    provider->mExpectedTagSize = size;
    return true;
}

bool CryptoProvider::DecryptContext::update_aad(const uint8_t* data, size_t size) const
{
    return provider->updateAad(data, size);
}

bool CryptoProvider::DecryptContext::update_data(uint8_t* out, const uint8_t* data, size_t size, size_t* out_size) const
{
    return provider->updateData(out, data, size, out_size);
}

bool CryptoProvider::DecryptContext::finalize(uint8_t* out, size_t* out_size) const
{
    if (!provider->finish(out, out_size)) {
        return false;
    }

    if (provider->mIsGcm) {
        // mbedtls_gcm_finish computes the tag, we compare it to the one from the packet
        if (provider->mExpectedTagSize == 0) {
            return false;
        }
        return mbedtls_ct_memcmp(provider->mTag, provider->mExpectedTag, provider->mExpectedTagSize) == 0;
    }

    return true;
}

} // namespace srtc::compat

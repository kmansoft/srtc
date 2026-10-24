#pragma once

#include "srtc/srtp_util.h"

#include <mbedtls/aes.h>
#include <mbedtls/gcm.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace srtc::compat
{

class CryptoProvider
{
public:
    explicit CryptoProvider(ProfileId profileId);
    ~CryptoProvider();

    CryptoProvider(const CryptoProvider&) = delete;
    CryptoProvider(CryptoProvider&&) = delete;

    CryptoProvider& operator=(const CryptoProvider&) = delete;
    CryptoProvider& operator=(CryptoProvider&&) = delete;

    // The contexts are views over the provider's cipher state, valid until the next create*Context() call

    struct EncryptContext
    {
        CryptoProvider* provider;

        [[nodiscard]] explicit operator bool() const { return provider != nullptr; }

        bool update_aad(const uint8_t* data, size_t size) const;
        bool update_data(uint8_t* out, const uint8_t* data, size_t size, size_t* out_size) const;

        bool finalize(uint8_t* out, size_t* out_size) const;

        bool get_digest(uint8_t* out, size_t size) const;
    };

    EncryptContext createEncryptContext(const uint8_t* key, const uint8_t* iv);

    struct DecryptContext
    {
        CryptoProvider* provider;

        [[nodiscard]] explicit operator bool() const { return provider != nullptr; }

        // GCM only, the expected tag must be set before finalize
        bool set_tag(const uint8_t* tag, size_t size) const;

        bool update_aad(const uint8_t* data, size_t size) const;
        bool update_data(uint8_t* out, const uint8_t* data, size_t size, size_t* out_size) const;

        // For GCM, this validates the tag
        bool finalize(uint8_t* out, size_t* out_size) const;
    };

    DecryptContext createDecryptContext(const uint8_t* key, const uint8_t* iv);

private:
    static constexpr size_t kGcmTagSize = 16;

    const bool mIsGcm;
    const size_t mKeySize;

    // AES GCM
    mbedtls_gcm_context mGcm;
    // The AAD can come in parts, but mbedtls_gcm_update_ad() only promises to accept it in one call
    std::vector<uint8_t> mAad;
    bool mIsAadFlushed;
    uint8_t mTag[kGcmTagSize];
    uint8_t mExpectedTag[kGcmTagSize];
    size_t mExpectedTagSize;

    // AES CTR
    mbedtls_aes_context mAes;
    unsigned char mCtrNonceCounter[16];
    unsigned char mCtrStreamBlock[16];
    size_t mCtrOffset;

    [[nodiscard]] bool start(const uint8_t* key, const uint8_t* iv, bool encrypt);
    [[nodiscard]] bool updateAad(const uint8_t* data, size_t size);
    [[nodiscard]] bool flushAad();
    [[nodiscard]] bool updateData(uint8_t* out, const uint8_t* data, size_t size, size_t* out_size);
    [[nodiscard]] bool finish(uint8_t* out, size_t* out_size);
};

void initCryptoProvider();

}

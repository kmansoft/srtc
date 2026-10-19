#pragma once

#include "srtc/srtp_util.h"

struct evp_cipher_ctx_st;
struct evp_cipher_st;

namespace srtc::providers
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

    struct EncryptContext
    {
        struct evp_cipher_ctx_st* cipher_ctx;

        [[nodiscard]] explicit operator bool() const { return cipher_ctx != nullptr; }

        bool update_aad(const uint8_t* data, size_t size) const;
        bool update_data(uint8_t* out, const uint8_t* data, size_t size, size_t* out_size) const;

        bool finalize(uint8_t* out, size_t* out_size) const;

        bool get_digest(uint8_t* out, size_t size) const;
    };

    EncryptContext createEncryptContext(const uint8_t* key, const uint8_t* iv);

    struct DecryptContext
    {
        struct evp_cipher_ctx_st* cipher_ctx;

        [[nodiscard]] explicit operator bool() const { return cipher_ctx != nullptr; }

        // GCM only, the expected tag must be set before finalize
        bool set_tag(const uint8_t* tag, size_t size) const;

        bool update_aad(const uint8_t* data, size_t size) const;
        bool update_data(uint8_t* out, const uint8_t* data, size_t size, size_t* out_size) const;

        // For GCM, this validates the tag
        bool finalize(uint8_t* out, size_t* out_size) const;
    };

    DecryptContext createDecryptContext(const uint8_t* key, const uint8_t* iv);

private:
    const ProfileId mProfileId;
    struct evp_cipher_ctx_st* mCipherCtx;

    [[nodiscard]] const struct evp_cipher_st* createCipher() const;
};

void initCryptoProvider();

// Constant time comparison
bool secureEquals(const void* a, const void* b, size_t size);

}

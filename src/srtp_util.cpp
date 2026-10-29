#include "srtc/srtp_util.h"

#include "crypto_util.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#if defined(SRTC_USE_OPENSSL)
#include <openssl/evp.h>
#elif defined(SRTC_USE_MBEDTLS)
#include <mbedtls/aes.h>
#endif

namespace srtc
{

// ----- CryptoBytes

CryptoBytes::CryptoBytes()
    : mSize(0)
{
}

CryptoBytes::~CryptoBytes()
{
    compat::secureZero(v8, sizeof(v8));
}

void CryptoBytes::clear()
{
    compat::secureZero(v8, sizeof(v8));
    mSize = 0;
}

void CryptoBytes::assign(const uint8_t* ptr, size_t size)
{
    assert(size <= sizeof(v8));

    mSize = std::min(sizeof(v8), size);

    size_t i = 0;
    while (i < mSize) {
        v8[i] = ptr[i];
        i += 1;
    }

    while (i < sizeof(v8)) {
        v8[i] = 0;
        i += 1;
    }
}

uint8_t* CryptoBytes::data()
{
    return v8;
}

const uint8_t* CryptoBytes::data() const
{
    return v8;
}

bool CryptoBytes::empty() const
{
    return mSize == 0;
}

size_t CryptoBytes::size() const
{
    return mSize;
}

CryptoBytes& CryptoBytes::operator^=(const CryptoBytes& other)
{
    for (size_t i = 0; i < sizeof(v8); i += 1) {
        v8[i] ^= other.v8[i];
    }

    return *this;
}

// ----- CryptoWriter

CryptoWriter::CryptoWriter(CryptoBytes& bytes)
    : mBytes(bytes)
{
    mBytes.clear();
}

void CryptoWriter::writeU8(uint8_t value)
{
    assert(mBytes.mSize <= sizeof(mBytes.v8) - 1);
    if (mBytes.mSize <= sizeof(mBytes.v8) - 1) {
        mBytes.v8[mBytes.mSize++] = value;
    }
}

void CryptoWriter::writeU16(uint16_t value)
{
    assert(mBytes.mSize <= sizeof(mBytes.v8) - 2);
    if (mBytes.mSize <= sizeof(mBytes.v8) - 2) {
        mBytes.v8[mBytes.mSize++] = (value >> 8) & 0xff;
        mBytes.v8[mBytes.mSize++] = value & 0xff;
    }
}

void CryptoWriter::writeU32(uint32_t value)
{
    assert(mBytes.mSize <= sizeof(mBytes.v8) - 4);
    if (mBytes.mSize <= sizeof(mBytes.v8) - 4) {
        mBytes.v8[mBytes.mSize++] = (value >> 24) & 0xff;
        mBytes.v8[mBytes.mSize++] = (value >> 16) & 0xff;
        mBytes.v8[mBytes.mSize++] = (value >> 8) & 0xff;
        mBytes.v8[mBytes.mSize++] = value & 0xff;
    }
}

void CryptoWriter::append(const uint8_t* data, size_t size)
{
    assert(mBytes.mSize <= sizeof(mBytes.v8) - size);
    if (mBytes.mSize <= sizeof(mBytes.v8) - size) {
        std::memcpy(mBytes.v8 + mBytes.mSize, data, size);
        mBytes.mSize += size;
    }
}

// ----- KeyDerivation

bool KeyDerivation::generate(const CryptoBytes& masterKey,
                             const CryptoBytes& masterSalt,
                             uint8_t label,
                             CryptoBytes& output,
                             size_t desiredOutputSize)
{
    assert(masterKey.size() == 16 || masterKey.size() == 32);
    assert(masterSalt.size() == 12 || masterSalt.size() == 14);

    assert(desiredOutputSize > 0 && desiredOutputSize <= 32);

    // https://datatracker.ietf.org/doc/html/rfc3711#appendix-B.3

    uint8_t input[16] = {};
    std::memcpy(input, masterSalt.data(), masterSalt.size());
    input[7] ^= label;

    uint8_t outbuf[16];

    const auto blockCount = (desiredOutputSize + 15) / 16;
    bool res = false;

    CryptoWriter outputWriter(output);

    // Each block is a single AES-ECB encryption with the master key
#if defined(SRTC_USE_OPENSSL)
    const EVP_CIPHER* cipher;
    switch (masterKey.size()) {
    case 16:
        cipher = EVP_aes_128_ecb();
        break;
    case 32:
        cipher = EVP_aes_256_ecb();
        break;
    default:
        return false;
    }

    const auto ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return false;
    }
#elif defined(SRTC_USE_MBEDTLS)
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    if (mbedtls_aes_setkey_enc(&ctx, masterKey.data(), static_cast<unsigned int>(masterKey.size() * 8)) != 0) {
        goto fail;
    }
#endif

#if defined(SRTC_USE_OPENSSL)
    // AES-ECB: initialize once, each EVP_EncryptUpdate call is independent (no chaining)
    if (!EVP_EncryptInit_ex(ctx, cipher, nullptr, masterKey.data(), nullptr)) {
        goto fail;
    }
#endif

    for (auto blockIndex = 0u; blockIndex < blockCount; blockIndex += 1) {
        input[14] = (blockIndex >> 8) & 0xff;
        input[15] = blockIndex & 0xff;

#if defined(SRTC_USE_OPENSSL)
        int outlen = 0;
        if (!EVP_EncryptUpdate(ctx, outbuf, &outlen, input, 16)) {
            goto fail;
        }
        assert(outlen == 16);
#elif defined(SRTC_USE_MBEDTLS)
        if (mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, input, outbuf) != 0) {
            goto fail;
        }
#endif

        outputWriter.append(outbuf, (blockIndex < blockCount - 1) ? 16 : (desiredOutputSize - 16 * blockIndex));
    }
    res = true;

fail:
#if defined(SRTC_USE_OPENSSL)
    EVP_CIPHER_CTX_free(ctx);
#elif defined(SRTC_USE_MBEDTLS)
    mbedtls_aes_free(&ctx);
#endif
    return res;
}

} // namespace srtc

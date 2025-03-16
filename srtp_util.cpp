#include "srtc/srtp_util.h"

#include <cstring>
#include <cassert>
#include <mutex>

#include <openssl/evp.h>
#include <openssl/err.h>

namespace {

std::once_flag gInitFlag;

void initOpenSSL() {
    std::call_once(gInitFlag, []{
        OpenSSL_add_all_algorithms();
        OpenSSL_add_all_ciphers();
        OpenSSL_add_all_digests();
        ERR_load_crypto_strings();
    });
}

}

namespace srtc {

// ----- CryptoBytes

CryptoBytes::CryptoBytes()
    : mSize(0)
{
}

void CryptoBytes::clear()
{
    std::memset(v8, 0, sizeof(v8));
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

// ----- CryptoBytesWriter

CryptoBytesWriter::CryptoBytesWriter(CryptoBytes& bytes)
    : mBytes(bytes)
{
    mBytes.clear();
}

void CryptoBytesWriter::writeU8(uint8_t value)
{
    assert(mBytes.mSize <= sizeof(mBytes.v8) - 1);
    if (mBytes.mSize <= sizeof(mBytes.v8) - 1) {
        mBytes.v8[mBytes.mSize++] = value;
    }
}

void CryptoBytesWriter::writeU16(uint16_t value)
{
    assert(mBytes.mSize <= sizeof(mBytes.v8) - 2);
    if (mBytes.mSize <= sizeof(mBytes.v8) - 2) {
        mBytes.v8[mBytes.mSize++] = (value >> 8) & 0xff;
        mBytes.v8[mBytes.mSize++] = value & 0xff;
    }
}

void CryptoBytesWriter::writeU32(uint32_t value)
{
    assert(mBytes.mSize <= sizeof(mBytes.v8) - 4);
    if (mBytes.mSize <= sizeof(mBytes.v8) - 4) {
        mBytes.v8[mBytes.mSize++] = (value >> 24) & 0xff;
        mBytes.v8[mBytes.mSize++] = (value >> 16) & 0xff;
        mBytes.v8[mBytes.mSize++] = (value >> 8) & 0xff;
        mBytes.v8[mBytes.mSize++] = value & 0xff;
    }
}

void CryptoBytesWriter::append(const uint8_t* data, size_t size)
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
                             srtc::CryptoBytes& output,
                             size_t desiredOutputSize)
{
    assert(masterKey.size() == 16 || masterKey.size() == 32);
    assert(masterSalt.size() == 12 || masterSalt.size() == 14);

    assert(desiredOutputSize > 0 && desiredOutputSize <= 32);

    // https://datatracker.ietf.org/doc/html/rfc3711#appendix-B.3

    uint8_t input[16] = {};
    std::memcpy(input, masterSalt.data(), masterSalt.size());
    input[7] ^= label;

    uint8_t zeroes[16] = {};

    uint8_t outbuf[16];
    int outlen = 0;

    const auto blockCount = (desiredOutputSize + 15) / 16;
    bool res = false;

    srtc::CryptoBytesWriter outputWriter(output);

    const auto ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        goto fail;
    }

    const EVP_CIPHER* cipher;
    switch (masterKey.size()) {
        case 16:
            cipher = EVP_aes_128_ctr();
            break;
        case 32:
            cipher = EVP_aes_256_ctr();
            break;
        default:
            goto fail;
    }

    for (auto blockIndex = 0; blockIndex < blockCount; blockIndex += 1) {
        input[14] = (blockIndex >> 16) & 0xff;
        input[15] = blockIndex & 0xff;

        if (!EVP_EncryptInit_ex(ctx, cipher, nullptr, masterKey.data(), input)) {
            goto fail;
        }
        if (!EVP_EncryptUpdate(ctx, outbuf, &outlen, zeroes, 16)) {
            goto fail;
        }
        assert(outlen == 16);

        outputWriter.append(outbuf,
        (blockIndex < blockCount - 1)
                ? 16
                : (desiredOutputSize - 16 * blockIndex));
    }
    res = true;

fail:
    EVP_CIPHER_CTX_free(ctx);
    return res;
}

}

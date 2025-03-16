// Some code in this file is based on Cisco's libSRTP
// https://github.com/cisco/libsrtp
// Copyright (c) 2001-2017 Cisco Systems, Inc.
// All rights reserved.
// Neither the name of the Cisco Systems, Inc. nor the names of its
// contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.

#include "srtc/srtp_crypto.h"
#include "srtc/srtp_util.h"
#include "srtc/byte_buffer.h"
#include "srtc/srtp_openssl.h"

#include <arpa/inet.h>

#include <cstring>
#include <cassert>

#include <openssl/srtp.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace {

constexpr size_t kAESGCM_TagSize = 16;

constexpr uint32_t kRTCP_EncryptedBit = 0x80000000u;
constexpr size_t kRTCP_HeaderSize = 8u;
constexpr size_t kRTCP_TrailerSize = 4u;

}

namespace srtc {

std::pair<std::shared_ptr<SrtpCrypto>, Error> SrtpCrypto::create(
        uint16_t profileId,
        const CryptoBytes& receiveMasterKey,
        const CryptoBytes& receiveMasterSalt)
{
    initOpenSSL();

    if (receiveMasterKey.empty()) {
        return { nullptr, { Error::Code::InvalidData, "Master key is empty"}};
    }
    if (receiveMasterSalt.empty()) {
        return { nullptr, { Error::Code::InvalidData, "Master salt is empty"}};
    }

    switch (profileId) {
        case SRTP_AEAD_AES_256_GCM:
        case SRTP_AEAD_AES_128_GCM:
        case SRTP_AES128_CM_SHA1_80:
        case SRTP_AES128_CM_SHA1_32:
            break;
        default:
            return { nullptr, { Error::Code::InvalidData, "Invalid SRTP profile id"}};
    }

    CryptoBytes receiveRtcpKey, receiveRtcpAuth, receiveRtcpSalt;

    if (!KeyDerivation::generate(receiveMasterKey, receiveMasterSalt,
                                 KeyDerivation::kLabelRtcpKey,
                                 receiveRtcpKey,
                                 receiveMasterKey.size())
            ||
        ! KeyDerivation::generate(receiveMasterKey, receiveMasterSalt,
                                  KeyDerivation::kLabelRtcpAuth,
                                  receiveRtcpAuth,
                                  20)
            ||
        ! KeyDerivation::generate(receiveMasterKey, receiveMasterSalt,
                                  KeyDerivation::kLabelRtcpSalt,
                                  receiveRtcpSalt,
                                  receiveMasterSalt.size())
    ) {
        return { nullptr, { Error::Code::InvalidData, "Error generating derived keys or salts"}};
    }

    return {
            std::make_shared<SrtpCrypto>(profileId, receiveRtcpKey, receiveRtcpAuth, receiveRtcpSalt),
            Error::OK
    };

}

bool SrtpCrypto::unprotectReceiveRtcp(const ByteBuffer& packet,
                                      ByteBuffer& plain)
{
    plain.resize(0);

    switch (mProfileId) {
        case SRTP_AEAD_AES_256_GCM:
        case SRTP_AEAD_AES_128_GCM:
            return unprotectReceiveRtcpGCM(packet, plain);
        case SRTP_AES128_CM_SHA1_80:
        case SRTP_AES128_CM_SHA1_32:
            return unprotectReceiveRtcpCM(packet, plain);
        default:
            assert(false);
            return false;
    }
}

void SrtpCrypto::computeReceiveRtcpIV(CryptoBytes& iv,
                                      uint32_t ssrc,
                                      uint32_t seq)
{
    // https://datatracker.ietf.org/doc/html/rfc7714#section-9.1
    CryptoBytesWriter ivw(iv);

    if (mReceiveRtcpSalt.size() == 14) {
        ivw.writeU8(0);
        ivw.writeU8(0);
    }

    ivw.writeU8(0);
    ivw.writeU8(0);
    ivw.writeU32(ssrc);
    ivw.writeU8(0);
    ivw.writeU8(0);
    ivw.writeU32(seq);
    iv ^= mReceiveRtcpSalt;
}

bool SrtpCrypto::unprotectReceiveRtcpGCM(const ByteBuffer& packet,
                                         ByteBuffer& plain)
{
    const auto encryptedSize = packet.size();
    if (encryptedSize <= 4 + 4 + 16 + 4) {
        // 4 byte RTCP header
        // 4 byte SSRC
        // ... ciphertext
        // 16 byte AES GCM tag
        // 4 byte SRTCP index
        return false;
    }

    const auto ctx = mReceiveCipherCtx;
    if (!ctx) {
        return false;
    }

    const auto encrypedData = packet.data();

    // Extract ssrc and seq
    // https://datatracker.ietf.org/doc/html/rfc7714#section-9.2
    const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(encrypedData + 4));
    const uint32_t trailer = ntohl(*reinterpret_cast<const uint32_t*>(encrypedData + encryptedSize - 4));
    const uint32_t seq = ~kRTCP_EncryptedBit & trailer;
    const auto isEncrypted = (kRTCP_EncryptedBit & trailer) != 0;

    // Compute the IV
    CryptoBytes iv;
    computeReceiveRtcpIV(iv, ssrc, seq);

    // Output buffer
    const auto plainSize = encryptedSize - kAESGCM_TagSize - kRTCP_TrailerSize;
    plain.reserve(plainSize);

    const auto plainData = plain.data();

    int len = 0, plain_len = 0;
    int final_ret = 0;

    // Set key and iv
    if (!EVP_DecryptInit_ex(ctx, nullptr, nullptr, mReceiveRtcpKey.data(), iv.data())) {
        goto fail;
    }

    // Set tag
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kAESGCM_TagSize,
                             encrypedData + encryptedSize - kRTCP_TrailerSize - kAESGCM_TagSize)) {
        goto fail;
    }

    // AAD
    if (isEncrypted) {
        if (!EVP_DecryptUpdate(ctx, nullptr, &len, encrypedData, kRTCP_HeaderSize)) {
            goto fail;
        }
    } else {
        if (!EVP_DecryptUpdate(ctx, nullptr, &len, encrypedData,
                               static_cast<int>(encryptedSize - kAESGCM_TagSize - kRTCP_TrailerSize))) {
            goto fail;
        }
    }

    if (!EVP_DecryptUpdate(ctx, nullptr, &len, encrypedData + encryptedSize - kRTCP_TrailerSize, kRTCP_TrailerSize)) {
        goto fail;
    }

    // The header is not encrypted
    std::memcpy(plainData, encrypedData, kRTCP_HeaderSize);
    plain_len = kRTCP_HeaderSize;

    // Encrypted portion
    if (isEncrypted) {
        if (!EVP_DecryptUpdate(ctx, plainData + kRTCP_HeaderSize, &len, encrypedData + kRTCP_HeaderSize,
                               static_cast<int>(encryptedSize - kRTCP_HeaderSize - kAESGCM_TagSize - kRTCP_TrailerSize))) {
            goto fail;
        }
        plain_len += len;
    } else {
        std::memcpy(plainData + kRTCP_HeaderSize, encrypedData + kRTCP_HeaderSize,
                    encryptedSize - kRTCP_HeaderSize - kAESGCM_TagSize - kRTCP_TrailerSize);
        plain_len += static_cast<int>(encryptedSize - kRTCP_HeaderSize - kAESGCM_TagSize - kRTCP_TrailerSize);
    }

    // Finalize, this validated the TAG
    final_ret = EVP_DecryptFinal_ex(ctx, plainData + plain_len, &len);
    if (final_ret > 0) {
        plain_len += len;
    }

fail:
    if (final_ret > 0) {
        assert(plain_len == plainSize);
        plain.resize(plainSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::unprotectReceiveRtcpCM(const ByteBuffer& packet,
                                        ByteBuffer& plain)
{
    const size_t digestSize = 10; // for both SHA1_80 and SHA1_32

    const auto encryptedSize = packet.size();
    if (encryptedSize <= 4 + 4 + 4 + digestSize) {
        // 4 byte RTCP header
        // 4 byte SSRC
        // ... ciphertext
        // 4 byte SRTCP index
        // 10 byte HMAC-SHA1 digest
        return false;
    }

    const auto ctx = mReceiveCipherCtx;
    if (!ctx) {
        return false;
    }

    const auto encrypedData = packet.data();
    const auto digestPtr = encrypedData + encryptedSize - digestSize;

    uint8_t digest[20] = {};   // 160 bits
    unsigned int digest_len = 0;

    if (!HMAC(EVP_sha1(),
              mReceiveRtcpAuth.data(),
              static_cast<int>(mReceiveRtcpAuth.size()),
              encrypedData, digestPtr - encrypedData, digest, &digest_len)) {
        return false;
    }

    if (std::memcmp(digest, digestPtr, digestSize) != 0) {
        // Digest validation failed
        return false;
    }

    const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(encrypedData + 4));
    const uint32_t trailer = ntohl(*reinterpret_cast<const uint32_t*>(encrypedData + encryptedSize - digestSize - 4));
    const uint32_t seq = ~kRTCP_EncryptedBit & trailer;
    const auto isEncrypted = (kRTCP_EncryptedBit & trailer) != 0;

    // Compute the IV
    CryptoBytes iv;
    computeReceiveRtcpIV(iv, ssrc, seq);

    // Output buffer
    const auto plainSize = encryptedSize - digestSize - kRTCP_TrailerSize;
    plain.reserve(plainSize);

    const auto plainData = plain.data();

    int len = 0, plain_len = 0;
    int final_ret = 0;

    // The header is not encrypted
    std::memcpy(plainData, encrypedData, kRTCP_HeaderSize);
    plain_len = kRTCP_HeaderSize;

    // The main body
    if (isEncrypted) {
        // Set key and iv
        if (!EVP_DecryptInit_ex(ctx, nullptr, nullptr, mReceiveRtcpKey.data(), iv.data())) {
            goto fail;
        }

        // Decrypt
        if (!EVP_DecryptUpdate(ctx, plainData + kRTCP_HeaderSize, &len, encrypedData + kRTCP_HeaderSize,
                               static_cast<int>(encryptedSize - kRTCP_HeaderSize - kRTCP_TrailerSize - digestSize))) {
            goto fail;
        }
        plain_len += len;

        // Finalize
        final_ret = EVP_DecryptFinal_ex(ctx, plainData + plain_len, &len);
        if (final_ret > 0) {
            plain_len += len;
        }
    } else {
        std::memcpy(plainData + kRTCP_HeaderSize, encrypedData + kRTCP_HeaderSize,
                    encryptedSize - kRTCP_HeaderSize - kRTCP_TrailerSize - digestSize);
        plain_len += static_cast<int>(encryptedSize - kRTCP_HeaderSize - kRTCP_TrailerSize - digestSize);
        final_ret = 1;
    }

    fail:
    if (final_ret > 0) {
        assert(plain_len == plainSize);
        plain.resize(plainSize);
        return true;
    }
    return false;
}

SrtpCrypto::SrtpCrypto(uint16_t profileId,
                       const CryptoBytes& receiveRtcpKey,
                       const CryptoBytes& receiveRtcpAuth,
                       const CryptoBytes& receiveRtcpSalt)
    : mProfileId(profileId)
    , mReceiveRtcpKey(receiveRtcpKey)
    , mReceiveRtcpAuth(receiveRtcpAuth)
    , mReceiveRtcpSalt(receiveRtcpSalt)
{
    mReceiveCipherCtx = EVP_CIPHER_CTX_new();

    const EVP_CIPHER* cipher;
    switch (mProfileId) {
        case SRTP_AEAD_AES_256_GCM:
            cipher = EVP_aes_256_gcm();
            break;
        case SRTP_AEAD_AES_128_GCM:
            cipher = EVP_aes_128_gcm();
            break;
        case SRTP_AES128_CM_SHA1_80:
        case SRTP_AES128_CM_SHA1_32:
            cipher = EVP_aes_128_ctr();
            break;
        default:
            cipher = nullptr;
            break;
    }

    if (cipher) {
        EVP_DecryptInit_ex(mReceiveCipherCtx, cipher, nullptr, nullptr, nullptr);
    }
}

SrtpCrypto::~SrtpCrypto()
{
    if (mReceiveCipherCtx) {
        EVP_CIPHER_CTX_free(mReceiveCipherCtx);
    }
}

}

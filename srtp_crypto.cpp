// Some code in this file is based on Cisco's libSRTP
// https://github.com/cisco/libsrtp
// Copyright (c) 2001-2017 Cisco Systems, Inc.
// All rights reserved.
// Neither the name of the Cisco Systems, Inc. nor the names of its
// contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.

#include "srtc/srtp_crypto.h"
#include "srtc/byte_buffer.h"
#include "srtc/srtp_hmac_sha1.h"
#include "srtc/srtp_openssl.h"
#include "srtc/srtp_util.h"

#include <arpa/inet.h>

#include <cassert>
#include <cstring>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/srtp.h>

namespace
{

constexpr size_t kAESGCM_TagSize = 16;

constexpr uint16_t kRTP_ExtensionBit = 0x1000;

constexpr uint32_t kRTCP_EncryptedBit = 0x80000000u;
constexpr size_t kRTCP_HeaderSize = 8u;
constexpr size_t kRTCP_TrailerSize = 4u;

} // namespace

namespace srtc
{

std::pair<std::shared_ptr<SrtpCrypto>, Error> SrtpCrypto::create(uint16_t profileId,
                                                                 const CryptoBytes& sendMasterKey,
                                                                 const CryptoBytes& sendMasterSalt,
                                                                 const CryptoBytes& receiveMasterKey,
                                                                 const CryptoBytes& receiveMasterSalt)
{
    initOpenSSL();

    if (sendMasterKey.empty() || receiveMasterKey.empty()) {
        return { nullptr, { Error::Code::InvalidData, "Master key is empty" } };
    }
    if (sendMasterSalt.empty() || receiveMasterSalt.empty()) {
        return { nullptr, { Error::Code::InvalidData, "Master salt is empty" } };
    }

    switch (profileId) {
    case SRTP_AEAD_AES_256_GCM:
    case SRTP_AEAD_AES_128_GCM:
    case SRTP_AES128_CM_SHA1_80:
    case SRTP_AES128_CM_SHA1_32:
        break;
    default:
        return { nullptr, { Error::Code::InvalidData, "Invalid SRTP profile id" } };
    }

    // Send RTP
    CryptoVectors sendRtp;
    if (!KeyDerivation::generate(
            sendMasterKey, sendMasterSalt, KeyDerivation::kLabelRtpKey, sendRtp.key, sendMasterKey.size()) ||
        !KeyDerivation::generate(sendMasterKey, sendMasterSalt, KeyDerivation::kLabelRtpAuth, sendRtp.auth, 20) ||
        !KeyDerivation::generate(
            sendMasterKey, sendMasterSalt, KeyDerivation::kLabelRtpSalt, sendRtp.salt, sendMasterSalt.size())) {
        return { nullptr, { Error::Code::InvalidData, "Error generating derived keys or salts" } };
    }

    // Send RTCP
    CryptoVectors sendRtcp;
    if (!KeyDerivation::generate(
            sendMasterKey, sendMasterSalt, KeyDerivation::kLabelRtcpKey, sendRtcp.key, sendMasterKey.size()) ||
        !KeyDerivation::generate(sendMasterKey, sendMasterSalt, KeyDerivation::kLabelRtcpAuth, sendRtcp.auth, 20) ||
        !KeyDerivation::generate(
            sendMasterKey, sendMasterSalt, KeyDerivation::kLabelRtcpSalt, sendRtcp.salt, sendMasterSalt.size())) {
        return { nullptr, { Error::Code::InvalidData, "Error generating derived keys or salts" } };
    }

    // Receive RTCP
    CryptoVectors receiveRtcp;
    if (!KeyDerivation::generate(receiveMasterKey,
                                 receiveMasterSalt,
                                 KeyDerivation::kLabelRtcpKey,
                                 receiveRtcp.key,
                                 receiveMasterKey.size()) ||
        !KeyDerivation::generate(
            receiveMasterKey, receiveMasterSalt, KeyDerivation::kLabelRtcpAuth, receiveRtcp.auth, 20) ||
        !KeyDerivation::generate(receiveMasterKey,
                                 receiveMasterSalt,
                                 KeyDerivation::kLabelRtcpSalt,
                                 receiveRtcp.salt,
                                 receiveMasterSalt.size())) {
        return { nullptr, { Error::Code::InvalidData, "Error generating derived keys or salts" } };
    }

    return { std::make_shared<SrtpCrypto>(profileId, sendRtp, sendRtcp, receiveRtcp), Error::OK };
}

size_t SrtpCrypto::getMediaProtectionOverhead() const
{
    switch (mProfileId) {
    case SRTP_AEAD_AES_256_GCM:
    case SRTP_AEAD_AES_128_GCM:
        return kAESGCM_TagSize;
    case SRTP_AES128_CM_SHA1_80:
        return 10;
    case SRTP_AES128_CM_SHA1_32:
        return 4;
    default:
        assert(false);
        return 0;
    }
}

bool SrtpCrypto::protectSendRtp(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& encrypted)
{
    encrypted.resize(0);

    switch (mProfileId) {
    case SRTP_AEAD_AES_256_GCM:
    case SRTP_AEAD_AES_128_GCM:
        return protectSendRtpGCM(packet, rolloverCount, encrypted);
    case SRTP_AES128_CM_SHA1_80:
    case SRTP_AES128_CM_SHA1_32:
        return protectSendRtpCM(packet, rolloverCount, encrypted);
    default:
        assert(false);
        return false;
    }
}

bool SrtpCrypto::protectSendRtpGCM(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& encrypted)
{
    const auto ctx = mSendCipherCtx;
    if (!ctx) {
        return false;
    }

    const size_t digestSize = kAESGCM_TagSize;

    const auto packetData = packet.data();
    const auto packetSize = packet.size();

    const uint16_t header = htons(*reinterpret_cast<const uint16_t*>(packetData));
    const uint16_t sequence = ntohs(*reinterpret_cast<const uint16_t*>(packetData + 2));
    const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(packetData + 8));

    // https://datatracker.ietf.org/doc/html/rfc7714#section-8.1
    CryptoBytes iv;
    CryptoWriter ivw(iv);
    ivw.writeU16(0);
    ivw.writeU32(ssrc);
    ivw.writeU32(rolloverCount);
    ivw.writeU16(sequence);
    iv ^= mSendRtp.salt;

    // https://datatracker.ietf.org/doc/html/rfc7714#section-7.1
    const auto encryptedSize = packetSize + digestSize;

    encrypted.reserve(encryptedSize);
    const auto encryptedData = encrypted.data();

    // The header is not encrypted
    auto headerSize = 4 + 4 + 4;
    if ((header & kRTP_ExtensionBit) != 0) {
        const auto extensionSize = ntohs(*reinterpret_cast<const uint16_t*>(packetData + 14));
        headerSize += 4;
        headerSize += extensionSize * 4;

        if (headerSize >= packetSize) {
            // The header reaches the end of payload (empty payload) or extends past its end
            return false;
        }
    }
    std::memcpy(encryptedData, packetData, headerSize);

    // Encryption
    int len = 0, total_len = 0;
    int final_ret = 0;
    uint8_t digest[kAESGCM_TagSize] = {};

    // Set key and iv
    if (!EVP_EncryptInit_ex(ctx, nullptr, nullptr, mSendRtp.key.data(), iv.data())) {
        goto fail;
    }

    // The header is AAD
    if (!EVP_EncryptUpdate(ctx, nullptr, &len, packetData, headerSize)) {
        goto fail;
    }

    // Encrypt the body
    if (!EVP_EncryptUpdate(ctx,
                           encryptedData + headerSize,
                           &len,
                           packetData + headerSize,
                           static_cast<int>(packetSize - headerSize))) {
        goto fail;
    }
    total_len = len;

    final_ret = EVP_EncryptFinal_ex(ctx, encryptedData + headerSize + len, &len);
    if (final_ret > 0) {
        total_len += len;
    }

    // Get tag
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kAESGCM_TagSize, digest)) {
        final_ret = 0;
        goto fail;
    }

    // Copy it to after the data
    std::memcpy(encryptedData + encryptedSize - digestSize, digest, kAESGCM_TagSize);

fail:
    if (final_ret > 0) {
        assert(total_len + headerSize + digestSize == encryptedSize);
        encrypted.resize(encryptedSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::protectSendRtpCM(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& encrypted)
{
    const auto ctx = mSendCipherCtx;
    if (!ctx) {
        return false;
    }

    size_t digestSize;
    switch (mProfileId) {
    case SRTP_AES128_CM_SHA1_80:
        digestSize = 10;
        break;
    case SRTP_AES128_CM_SHA1_32:
        digestSize = 4;
        break;
    default:
        assert(false);
        return false;
    }

    const auto packetData = packet.data();
    const auto packetSize = packet.size();

    const uint16_t header = htons(*reinterpret_cast<const uint16_t*>(packetData));
    const uint16_t sequence = ntohs(*reinterpret_cast<const uint16_t*>(packetData + 2));
    const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(packetData + 8));

    // https://datatracker.ietf.org/doc/html/rfc3711#section-4.1.1
    CryptoBytes iv;
    CryptoWriter ivw(iv);
    ivw.writeU32(0);
    ivw.writeU32(ssrc);
    ivw.writeU32(rolloverCount);
    ivw.writeU16(sequence);
    ivw.writeU16(0);
    iv ^= mSendRtp.salt;

    // https://datatracker.ietf.org/doc/html/rfc3711#section-3.1
    const auto encryptedSize = packetSize + digestSize;

    encrypted.reserve(encryptedSize);
    const auto encryptedData = encrypted.data();

    // The header is not encrypted
    auto headerSize = 4 + 4 + 4;
    if ((header & kRTP_ExtensionBit) != 0) {
        const auto extensionSize = ntohs(*reinterpret_cast<const uint16_t*>(packetData + 14));
        headerSize += 4;
        headerSize += extensionSize * 4;

        if (headerSize >= packetSize) {
            // The header reaches the end of payload (empty payload) or extends past its end
            return false;
        }
    }

    std::memcpy(encryptedData, packetData, headerSize);

    // We will need the trailer for the authentication tag
    const uint32_t trailer = htonl(rolloverCount);

    // Encryption
    int len = 0, total_len = 0;
    int final_ret = 0;
    uint8_t digest[20] = {};

    // Set key and iv
    if (!EVP_EncryptInit_ex(ctx, nullptr, nullptr, mSendRtp.key.data(), iv.data())) {
        goto fail;
    }

    // Encrypt the body
    if (!EVP_EncryptUpdate(ctx,
                           encryptedData + headerSize,
                           &len,
                           packetData + headerSize,
                           static_cast<int>(packetSize - headerSize))) {
        goto fail;
    }
    total_len = len;

    final_ret = EVP_EncryptFinal_ex(ctx, encryptedData + headerSize + len, &len);
    if (final_ret > 0) {
        total_len += len;
    }

    // Authentication tag, https://datatracker.ietf.org/doc/html/rfc3711#section-4.2
    if (!mHmacSha1->reset(mSendRtp.auth.data(), mSendRtp.auth.size())) {
        final_ret = 0;
        goto fail;
    }
    mHmacSha1->update(encryptedData, packetSize);
    mHmacSha1->update(reinterpret_cast<const uint8_t*>(&trailer), sizeof(trailer));
    mHmacSha1->final(digest);

    std::memcpy(encryptedData + encryptedSize - digestSize, digest, digestSize);

fail:
    if (final_ret > 0) {
        assert(total_len + headerSize + digestSize == encryptedSize);
        encrypted.resize(encryptedSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::protectSendRtcp(const ByteBuffer& packet, uint32_t seq, ByteBuffer& encrypted)
{
    encrypted.resize(0);

    switch (mProfileId) {
    case SRTP_AEAD_AES_256_GCM:
    case SRTP_AEAD_AES_128_GCM:
        return protectSendRtcpGCM(packet, seq, encrypted);
    case SRTP_AES128_CM_SHA1_80:
    case SRTP_AES128_CM_SHA1_32:
        return protectSendRtcpCM(packet, seq, encrypted);
    default:
        assert(false);
        return false;
    }
}

bool SrtpCrypto::protectSendRtcpGCM(const ByteBuffer& packet, uint32_t seq, ByteBuffer& encrypted)
{
    const auto ctx = mSendCipherCtx;
    if (!ctx) {
        return false;
    }

    const auto packetData = packet.data();
    const auto packetSize = packet.size();

    const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(packetData + 4));
    const uint32_t trailer = htonl(seq | kRTCP_EncryptedBit);

    // Compute the IV
    // https://datatracker.ietf.org/doc/html/rfc7714#section-9.1
    CryptoBytes iv;
    CryptoWriter ivw(iv);
    ivw.writeU16(0);
    ivw.writeU32(ssrc);
    ivw.writeU8(0);
    ivw.writeU8(0);
    ivw.writeU32(seq);
    iv ^= mSendRtcp.salt;

    // https://datatracker.ietf.org/doc/html/rfc3711#section-3.1
    const auto trailerSize = kRTCP_TrailerSize;
    const auto encryptedSize = packetSize + kAESGCM_TagSize + trailerSize;

    encrypted.reserve(encryptedSize);
    const auto encryptedData = encrypted.data();

    // The header is not encrypted
    const auto headerSize = kRTCP_HeaderSize;

    std::memcpy(encryptedData, packetData, headerSize);

    // Encryption
    int len = 0, total_len = 0;
    int final_ret = 0;
    uint8_t digest[20] = {};

    // Set key and iv
    if (!EVP_EncryptInit_ex(ctx, nullptr, nullptr, mSendRtcp.key.data(), iv.data())) {
        goto fail;
    }

    // AAD
    if (!EVP_EncryptUpdate(ctx, nullptr, &len, packetData, headerSize)) {
        goto fail;
    }

    if (!EVP_EncryptUpdate(
            ctx, nullptr, &len, reinterpret_cast<const uint8_t*>(&trailer), static_cast<int>(trailerSize))) {
        goto fail;
    }

    // Encrypt the body
    if (!EVP_EncryptUpdate(ctx,
                           encryptedData + headerSize,
                           &len,
                           packetData + headerSize,
                           static_cast<int>(packetSize - headerSize))) {
        goto fail;
    }
    total_len = len;

    final_ret = EVP_EncryptFinal_ex(ctx, encryptedData + headerSize + len, &len);
    if (final_ret > 0) {
        total_len += len;
    }

    // Get tag
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kAESGCM_TagSize, digest)) {
        final_ret = 0;
        goto fail;
    }

    // Copy it to after the data
    std::memcpy(encryptedData + encryptedSize - kAESGCM_TagSize - trailerSize, digest, kAESGCM_TagSize);

    // Trailer
    std::memcpy(encryptedData + encryptedSize - trailerSize, &trailer, trailerSize);

fail:
    if (final_ret > 0) {
        assert(total_len + headerSize + kAESGCM_TagSize + trailerSize == encryptedSize);
        encrypted.resize(encryptedSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::protectSendRtcpCM(const ByteBuffer& packet, uint32_t seq, ByteBuffer& encrypted)
{
    const size_t digestSize = 10; // for both SHA1_80 and SHA1_32

    const auto ctx = mSendCipherCtx;
    if (!ctx) {
        return false;
    }

    const auto packetData = packet.data();
    const auto packetSize = packet.size();

    const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(packetData + 4));
    const uint32_t trailer = htonl(seq | kRTCP_EncryptedBit);

    // Compute the IV
    CryptoBytes iv;
    CryptoWriter ivw(iv);

    ivw.writeU16(0);
    ivw.writeU16(0);
    ivw.writeU32(ssrc);
    ivw.writeU8(0);
    ivw.writeU8(0);
    ivw.writeU32(seq);
    iv ^= mSendRtcp.salt;

    // https://datatracker.ietf.org/doc/html/rfc3711#section-3.1
    const auto trailerSize = kRTCP_TrailerSize;
    const auto encryptedSize = packetSize + trailerSize + digestSize;

    encrypted.reserve(encryptedSize);
    const auto encryptedData = encrypted.data();

    // The header is not encrypted
    const auto headerSize = kRTCP_HeaderSize;

    std::memcpy(encryptedData, packetData, headerSize);

    // Encryption
    int len = 0, total_len = 0;
    int final_ret = 0;
    uint8_t digest[20] = {};

    // Set key and iv
    if (!EVP_EncryptInit_ex(ctx, nullptr, nullptr, mSendRtcp.key.data(), iv.data())) {
        goto fail;
    }

    // Encrypt the body
    if (!EVP_EncryptUpdate(ctx,
                           encryptedData + headerSize,
                           &len,
                           packetData + headerSize,
                           static_cast<int>(packetSize - headerSize))) {
        goto fail;
    }
    total_len = len;

    final_ret = EVP_EncryptFinal_ex(ctx, encryptedData + headerSize + len, &len);
    if (final_ret > 0) {
        total_len += len;
    }

    // Trailer
    std::memcpy(encryptedData + encryptedSize - trailerSize - digestSize, &trailer, trailerSize);

    // Authentication tag, https://datatracker.ietf.org/doc/html/rfc3711#section-4.2
    if (!mHmacSha1->reset(mSendRtcp.auth.data(), mSendRtcp.auth.size())) {
        final_ret = 0;
        goto fail;
    }
    mHmacSha1->update(encryptedData, packetSize + trailerSize);
    mHmacSha1->final(digest);

    std::memcpy(encryptedData + encryptedSize - digestSize, digest, digestSize);

fail:
    if (final_ret > 0) {
        assert(total_len + headerSize + trailerSize + digestSize == encryptedSize);
        encrypted.resize(encryptedSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::unprotectReceiveRtcp(const ByteBuffer& packet, ByteBuffer& plain)
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

bool SrtpCrypto::unprotectReceiveRtcpGCM(const ByteBuffer& packet, ByteBuffer& plain)
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
    const uint32_t seq = trailer & ~kRTCP_EncryptedBit;
    const auto isEncrypted = (trailer & kRTCP_EncryptedBit) != 0;

    // Compute the IV
    // https://datatracker.ietf.org/doc/html/rfc7714#section-9.1
    CryptoBytes iv;
    CryptoWriter ivw(iv);
    ivw.writeU16(0);
    ivw.writeU32(ssrc);
    ivw.writeU8(0);
    ivw.writeU8(0);
    ivw.writeU32(seq);
    iv ^= mReceiveRtcp.salt;

    // Output buffer
    const auto plainSize = encryptedSize - kAESGCM_TagSize - kRTCP_TrailerSize;
    plain.reserve(plainSize);

    const auto plainData = plain.data();

    int len = 0, plain_len = 0;
    int final_ret = 0;

    // Set key and iv
    if (!EVP_DecryptInit_ex(ctx, nullptr, nullptr, mReceiveRtcp.key.data(), iv.data())) {
        goto fail;
    }

    // Set tag
    if (!EVP_CIPHER_CTX_ctrl(ctx,
                             EVP_CTRL_GCM_SET_TAG,
                             kAESGCM_TagSize,
                             encrypedData + encryptedSize - kRTCP_TrailerSize - kAESGCM_TagSize)) {
        goto fail;
    }

    // AAD
    if (isEncrypted) {
        if (!EVP_DecryptUpdate(ctx, nullptr, &len, encrypedData, kRTCP_HeaderSize)) {
            goto fail;
        }
    } else {
        if (!EVP_DecryptUpdate(ctx,
                               nullptr,
                               &len,
                               encrypedData,
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
        if (!EVP_DecryptUpdate(
                ctx,
                plainData + kRTCP_HeaderSize,
                &len,
                encrypedData + kRTCP_HeaderSize,
                static_cast<int>(encryptedSize - kRTCP_HeaderSize - kAESGCM_TagSize - kRTCP_TrailerSize))) {
            goto fail;
        }
        plain_len += len;
    } else {
        std::memcpy(plainData + kRTCP_HeaderSize,
                    encrypedData + kRTCP_HeaderSize,
                    encryptedSize - kRTCP_HeaderSize - kAESGCM_TagSize - kRTCP_TrailerSize);
        plain_len += static_cast<int>(encryptedSize - kRTCP_HeaderSize - kAESGCM_TagSize - kRTCP_TrailerSize);
    }

    // Finalize, this validates the GCM auth tag
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

bool SrtpCrypto::unprotectReceiveRtcpCM(const ByteBuffer& packet, ByteBuffer& plain)
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

    uint8_t digest[20] = {}; // 160 bits

    if (!mHmacSha1->reset(mReceiveRtcp.auth.data(), mReceiveRtcp.auth.size())) {
        return false;
    }
    mHmacSha1->update(encrypedData, digestPtr - encrypedData);
    mHmacSha1->final(digest);

    if (CRYPTO_memcmp(digest, digestPtr, digestSize) != 0) {
        // Digest validation failed
        return false;
    }

    const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(encrypedData + 4));
    const uint32_t trailer = ntohl(*reinterpret_cast<const uint32_t*>(encrypedData + encryptedSize - digestSize - 4));
    const uint32_t seq = trailer & ~kRTCP_EncryptedBit;
    const auto isEncrypted = (trailer & kRTCP_EncryptedBit) != 0;

    // Compute the IV
    CryptoBytes iv;
    CryptoWriter ivw(iv);

    ivw.writeU16(0);
    ivw.writeU16(0);
    ivw.writeU32(ssrc);
    ivw.writeU8(0);
    ivw.writeU8(0);
    ivw.writeU32(seq);
    iv ^= mReceiveRtcp.salt;

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
        if (!EVP_DecryptInit_ex(ctx, nullptr, nullptr, mReceiveRtcp.key.data(), iv.data())) {
            goto fail;
        }

        // Decrypt
        if (!EVP_DecryptUpdate(ctx,
                               plainData + kRTCP_HeaderSize,
                               &len,
                               encrypedData + kRTCP_HeaderSize,
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
        std::memcpy(plainData + kRTCP_HeaderSize,
                    encrypedData + kRTCP_HeaderSize,
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
                       const CryptoVectors& sendRtp,
                       const CryptoVectors& sendRtcp,
                       const CryptoVectors& receiveRtcp)
    : mProfileId(profileId)
    , mSendRtp(sendRtp)
    , mSendRtcp(sendRtcp)
    , mReceiveRtcp(receiveRtcp)
    , mSendCipherCtx(nullptr)
    , mReceiveCipherCtx(nullptr)
    , mHmacSha1(std::make_shared<HmacSha1>())
{
    const auto sendCipher = createCipher();
    if (sendCipher) {
        mSendCipherCtx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(mSendCipherCtx, sendCipher, nullptr, nullptr, nullptr);
    }

    const auto receiveCipher = createCipher();
    if (receiveCipher) {
        mReceiveCipherCtx = EVP_CIPHER_CTX_new();
        EVP_DecryptInit_ex(mReceiveCipherCtx, receiveCipher, nullptr, nullptr, nullptr);
    }
}

SrtpCrypto::~SrtpCrypto()
{
    if (mSendCipherCtx) {
        EVP_CIPHER_CTX_free(mSendCipherCtx);
    }
    if (mReceiveCipherCtx) {
        EVP_CIPHER_CTX_free(mReceiveCipherCtx);
    }
}

const EVP_CIPHER* SrtpCrypto::createCipher() const
{
    switch (mProfileId) {
    case SRTP_AEAD_AES_256_GCM:
        return EVP_aes_256_gcm();
    case SRTP_AEAD_AES_128_GCM:
        return EVP_aes_128_gcm();
    case SRTP_AES128_CM_SHA1_80:
    case SRTP_AES128_CM_SHA1_32:
        return EVP_aes_128_ctr();
    default:
        return nullptr;
    }
}

} // namespace srtc

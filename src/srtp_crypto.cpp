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
#include "srtc/srtp_keys.h"
#include "srtc/srtp_util.h"

#include "crypto_provider.h"

#include <cassert>
#include <cstring>

#ifdef _WIN32
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

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

std::pair<std::shared_ptr<SrtpCrypto>, Error> SrtpCrypto::create(const SrtpKeys& srtpKeys)
{
    providers::initCryptoProvider();

    if (srtpKeys.sendMasterKey.empty() || srtpKeys.receiveMasterKey.empty()) {
        return { nullptr, { Error::Code::InvalidData, "Master key is empty" } };
    }
    if (srtpKeys.sendMasterSalt.empty() || srtpKeys.receiveMasterSalt.empty()) {
        return { nullptr, { Error::Code::InvalidData, "Master salt is empty" } };
    }

    switch (srtpKeys.profileId) {
    case ProfileId::kAES_256_GCM:
    case ProfileId::kAES_128_GCM:
    case ProfileId::kAES_128_CM_SHA1_80:
    case ProfileId::kAES_128_CM_SHA1_32:
        break;
    default:
        return { nullptr, { Error::Code::InvalidData, "Invalid SRTP profile id" } };
    }

    // Send RTP
    CryptoVectors sendRtp;
    if (!KeyDerivation::generate(srtpKeys.sendMasterKey,
                                 srtpKeys.sendMasterSalt,
                                 KeyDerivation::kLabelRtpKey,
                                 sendRtp.key,
                                 srtpKeys.sendMasterKey.size()) ||
        !KeyDerivation::generate(
            srtpKeys.sendMasterKey, srtpKeys.sendMasterSalt, KeyDerivation::kLabelRtpAuth, sendRtp.auth, 20) ||
        !KeyDerivation::generate(srtpKeys.sendMasterKey,
                                 srtpKeys.sendMasterSalt,
                                 KeyDerivation::kLabelRtpSalt,
                                 sendRtp.salt,
                                 srtpKeys.sendMasterSalt.size())) {
        return { nullptr, { Error::Code::InvalidData, "Error generating derived keys or salts" } };
    }

    // Receive RTP
    CryptoVectors receiveRtp;
    if (!KeyDerivation::generate(srtpKeys.receiveMasterKey,
                                 srtpKeys.receiveMasterSalt,
                                 KeyDerivation::kLabelRtpKey,
                                 receiveRtp.key,
                                 srtpKeys.receiveMasterKey.size()) ||
        !KeyDerivation::generate(
            srtpKeys.receiveMasterKey, srtpKeys.receiveMasterSalt, KeyDerivation::kLabelRtpAuth, receiveRtp.auth, 20) ||
        !KeyDerivation::generate(srtpKeys.receiveMasterKey,
                                 srtpKeys.receiveMasterSalt,
                                 KeyDerivation::kLabelRtpSalt,
                                 receiveRtp.salt,
                                 srtpKeys.receiveMasterSalt.size())) {
        return { nullptr, { Error::Code::InvalidData, "Error generating derived keys or salts" } };
    }

    // Send RTCP
    CryptoVectors sendRtcp;
    if (!KeyDerivation::generate(srtpKeys.sendMasterKey,
                                 srtpKeys.sendMasterSalt,
                                 KeyDerivation::kLabelRtcpKey,
                                 sendRtcp.key,
                                 srtpKeys.sendMasterKey.size()) ||
        !KeyDerivation::generate(
            srtpKeys.sendMasterKey, srtpKeys.sendMasterSalt, KeyDerivation::kLabelRtcpAuth, sendRtcp.auth, 20) ||
        !KeyDerivation::generate(srtpKeys.sendMasterKey,
                                 srtpKeys.sendMasterSalt,
                                 KeyDerivation::kLabelRtcpSalt,
                                 sendRtcp.salt,
                                 srtpKeys.sendMasterSalt.size())) {
        return { nullptr, { Error::Code::InvalidData, "Error generating derived keys or salts" } };
    }

    // Receive RTCP
    CryptoVectors receiveRtcp;
    if (!KeyDerivation::generate(srtpKeys.receiveMasterKey,
                                 srtpKeys.receiveMasterSalt,
                                 KeyDerivation::kLabelRtcpKey,
                                 receiveRtcp.key,
                                 srtpKeys.receiveMasterKey.size()) ||
        !KeyDerivation::generate(srtpKeys.receiveMasterKey,
                                 srtpKeys.receiveMasterSalt,
                                 KeyDerivation::kLabelRtcpAuth,
                                 receiveRtcp.auth,
                                 20) ||
        !KeyDerivation::generate(srtpKeys.receiveMasterKey,
                                 srtpKeys.receiveMasterSalt,
                                 KeyDerivation::kLabelRtcpSalt,
                                 receiveRtcp.salt,
                                 srtpKeys.receiveMasterSalt.size())) {
        return { nullptr, { Error::Code::InvalidData, "Error generating derived keys or salts" } };
    }

    return { std::make_shared<SrtpCrypto>(srtpKeys.profileId, sendRtp, receiveRtp, sendRtcp, receiveRtcp), Error::OK };
}

size_t SrtpCrypto::getMediaProtectionOverhead() const
{
    switch (mProfileId) {
    case ProfileId::kAES_256_GCM:
    case ProfileId::kAES_128_GCM:
        return kAESGCM_TagSize;
    case ProfileId::kAES_128_CM_SHA1_80:
        return 10;
    case ProfileId::kAES_128_CM_SHA1_32:
        return 4;
    default:
        assert(false);
        return 0;
    }
}

bool SrtpCrypto::protectSendMedia(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& encrypted)
{
    encrypted.resize(0);

    switch (mProfileId) {
    case ProfileId::kAES_256_GCM:
    case ProfileId::kAES_128_GCM:
        return protectSendMediaGCM(packet, rolloverCount, encrypted);
    case ProfileId::kAES_128_CM_SHA1_80:
    case ProfileId::kAES_128_CM_SHA1_32:
        return protectSendMediaCM(packet, rolloverCount, encrypted);
    default:
        assert(false);
        return false;
    }
}

bool SrtpCrypto::protectSendMediaGCM(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& encrypted)
{
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
    auto headerSize = 4u + 4 + 4;
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
    size_t len = 0, total_len = 0;
    bool success = false;
    uint8_t digest[kAESGCM_TagSize] = {};

    // Create context with key and iv
    auto context = mSendProvider->createEncryptContext(mSendRtp.key.data(), iv.data());
    if (!context) {
        goto fail;
    }

    // The header is AAD
    if (!context.update_aad(packetData, headerSize)) {
        goto fail;
    }

    // Encrypt the body
    if (!context.update_data(encryptedData + headerSize, packetData + headerSize, packetSize - headerSize, &len)) {
        goto fail;
    }
    total_len += len;

    if (!context.finalize(encryptedData + headerSize + len, &len)) {
        goto fail;
    }
    total_len += len;

    // Get tag and copy it after the data
    if (!context.get_digest(digest, kAESGCM_TagSize)) {
        goto fail;
    }
    std::memcpy(encryptedData + encryptedSize - digestSize, digest, kAESGCM_TagSize);

    // It all worked
    success = true;
fail:
    if (success) {
        assert(total_len + headerSize + digestSize == encryptedSize);
        (void)total_len;
        encrypted.resize(encryptedSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::protectSendMediaCM(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& encrypted)
{
    size_t digestSize;
    switch (mProfileId) {
    case ProfileId::kAES_128_CM_SHA1_80:
        digestSize = 10;
        break;
    case ProfileId::kAES_128_CM_SHA1_32:
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
    auto headerSize = 4u + 4 + 4;
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
    size_t len = 0, total_len = 0;
    bool success = false;
    uint8_t digest[20] = {};

    // Create context with key and iv
    auto context = mSendProvider->createEncryptContext(mSendRtp.key.data(), iv.data());
    if (!context) {
        goto fail;
    }

    // Encrypt the body
    if (!context.update_data(encryptedData + headerSize, packetData + headerSize, packetSize - headerSize, &len)) {
        goto fail;
    }
    total_len += len;

    if (!context.finalize(encryptedData + headerSize + len, &len)) {
        goto fail;
    }
    total_len += len;

    // Authentication tag, https://datatracker.ietf.org/doc/html/rfc3711#section-4.2
    if (!mHmacSha1->reset(mSendRtp.auth.data(), mSendRtp.auth.size())) {
        goto fail;
    }
    mHmacSha1->update(encryptedData, packetSize);
    mHmacSha1->update(reinterpret_cast<const uint8_t*>(&trailer), sizeof(trailer));
    mHmacSha1->final(digest);

    std::memcpy(encryptedData + encryptedSize - digestSize, digest, digestSize);

    // It all worked
    success = true;
fail:
    if (success) {
        assert(total_len + headerSize + digestSize == encryptedSize);
        (void)total_len;
        encrypted.resize(encryptedSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::unprotectReceiveMedia(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& plain)
{
    plain.resize(0);

    switch (mProfileId) {
    case ProfileId::kAES_256_GCM:
    case ProfileId::kAES_128_GCM:
        return unprotectReceiveMediaGCM(packet, rolloverCount, plain);
    case ProfileId::kAES_128_CM_SHA1_80:
    case ProfileId::kAES_128_CM_SHA1_32:
        return unprotectReceiveMediaCM(packet, rolloverCount, plain);
    default:
        assert(false);
        return false;
    }
}

bool SrtpCrypto::unprotectReceiveMediaGCM(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& plain)
{
    const size_t digestSize = kAESGCM_TagSize;

    const auto encryptedSize = packet.size();
    if (encryptedSize <= 4 + 4 + 4 + digestSize) {
        // 4 byte RTP header
        // 4 byte timestamp
        // 4 byte SSRC
        // ... ciphertext
        // 16 byte AES GCM tag
        return false;
    }

    const auto encryptedData = packet.data();

    const uint16_t header = htons(*reinterpret_cast<const uint16_t*>(encryptedData));
    const uint16_t sequence = ntohs(*reinterpret_cast<const uint16_t*>(encryptedData + 2));
    const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(encryptedData + 8));

    // https://datatracker.ietf.org/doc/html/rfc7714#section-8.1
    CryptoBytes iv;
    CryptoWriter ivw(iv);
    ivw.writeU16(0);
    ivw.writeU32(ssrc);
    ivw.writeU32(rolloverCount);
    ivw.writeU16(sequence);
    iv ^= mReceiveRtp.salt;

    // https://datatracker.ietf.org/doc/html/rfc7714#section-7.1
    const auto plainSize = encryptedSize - digestSize;

    plain.reserve(plainSize);
    const auto plainData = plain.data();

    // The header is not encrypted
    auto headerSize = 4u + 4 + 4;
    if ((header & kRTP_ExtensionBit) != 0) {
        if (encryptedSize < 4 + 4 + 4 + 4 + digestSize) {
            return false;
        }

        const auto extensionSize = ntohs(*reinterpret_cast<const uint16_t*>(encryptedData + 14));
        headerSize += 4;
        headerSize += extensionSize * 4;

        if (headerSize + digestSize >= encryptedSize) {
            // The header reaches the end of payload (empty payload) or extends past its end
            return false;
        }
    }
    std::memcpy(plainData, encryptedData, headerSize);

    // Decryption
    size_t len = 0, plain_len = 0;
    bool success = false;

    // Create context with key and iv
    auto context = mReceiveProvider->createDecryptContext(mReceiveRtp.key.data(), iv.data());
    if (!context) {
        goto fail;
    }

    // Set tag
    if (!context.set_tag(encryptedData + encryptedSize - digestSize, kAESGCM_TagSize)) {
        goto fail;
    }

    // The header is AAD
    if (!context.update_aad(encryptedData, headerSize)) {
        goto fail;
    }

    // Decrypt the body
    if (!context.update_data(
            plainData + headerSize, encryptedData + headerSize, encryptedSize - headerSize - digestSize, &len)) {
        goto fail;
    }
    plain_len += len;

    // Finalize, this validates the GCM auth tag
    if (!context.finalize(plainData + headerSize + len, &len)) {
        goto fail;
    }
    plain_len += len;

    // It all worked
    success = true;
fail:
    if (success) {
        assert(plain_len + headerSize == plainSize);
        (void)plain_len;
        plain.resize(plainSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::unprotectReceiveMediaCM(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& plain)
{
    size_t digestSize;
    switch (mProfileId) {
    case ProfileId::kAES_128_CM_SHA1_80:
        digestSize = 10;
        break;
    case ProfileId::kAES_128_CM_SHA1_32:
        digestSize = 4;
        break;
    default:
        assert(false);
        return false;
    }

    const auto encryptedSize = packet.size();
    if (encryptedSize <= 4 + 4 + 4 + digestSize) {
        // 4 byte RTP header
        // 4 byte timestamp
        // 4 byte SSRC
        // ... ciphertext
        // 4 or 10 bytes auth tag
        return false;
    }

    const auto encryptedData = packet.data();

    const uint16_t header = htons(*reinterpret_cast<const uint16_t*>(encryptedData));
    const uint16_t sequence = ntohs(*reinterpret_cast<const uint16_t*>(encryptedData + 2));
    const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(encryptedData + 8));

    const auto digestPtr = encryptedData + encryptedSize - digestSize;
    uint8_t digest[20] = {}; // 160 bits

    // https://datatracker.ietf.org/doc/html/rfc3711#section-4.1.1
    CryptoBytes iv;
    CryptoWriter ivw(iv);
    ivw.writeU32(0);
    ivw.writeU32(ssrc);
    ivw.writeU32(rolloverCount);
    ivw.writeU16(sequence);
    ivw.writeU16(0);
    iv ^= mReceiveRtp.salt;

    // https://datatracker.ietf.org/doc/html/rfc7714#section-7.1
    const auto plainSize = encryptedSize - digestSize;

    plain.reserve(plainSize);
    const auto plainData = plain.data();

    // The header is not encrypted
    auto headerSize = 4u + 4 + 4;
    if ((header & kRTP_ExtensionBit) != 0) {
        if (encryptedSize < 4 + 4 + 4 + 4 + digestSize) {
            return false;
        }

        const auto extensionSize = ntohs(*reinterpret_cast<const uint16_t*>(encryptedData + 14));
        headerSize += 4;
        headerSize += extensionSize * 4;

        if (headerSize + digestSize >= encryptedSize) {
            // The header reaches the end of payload (empty payload) or extends past its end
            return false;
        }
    }

    std::memcpy(plainData, encryptedData, headerSize);

    // We will need the trailer for the authentication tag
    const uint32_t trailer = htonl(rolloverCount);

    // Decryption
    size_t len = 0, plain_len = 0;
    bool success = false;

    // Create context with key and iv
    auto context = mReceiveProvider->createDecryptContext(mReceiveRtp.key.data(), iv.data());
    if (!context) {
        goto fail;
    }

    // Decrypt the body
    if (!context.update_data(
            plainData + headerSize, encryptedData + headerSize, encryptedSize - headerSize - digestSize, &len)) {
        goto fail;
    }
    plain_len += len;

    if (!context.finalize(plainData + headerSize + len, &len)) {
        goto fail;
    }
    plain_len += len;

    // Verify the digest
    if (!mHmacSha1->reset(mReceiveRtp.auth.data(), mReceiveRtp.auth.size())) {
        goto fail;
    }
    mHmacSha1->update(encryptedData, digestPtr - encryptedData);
    mHmacSha1->update(reinterpret_cast<const uint8_t*>(&trailer), sizeof(trailer));
    mHmacSha1->final(digest);

    if (!providers::secureEquals(digest, digestPtr, digestSize)) {
        // Digest validation failed
        goto fail;
    }

    // It all worked
    success = true;
fail:
    if (success) {
        assert(plain_len + headerSize == plainSize);
        (void)plain_len;
        plain.resize(plainSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::protectSendControl(const ByteBuffer& packet, uint32_t seq, ByteBuffer& encrypted)
{
    encrypted.resize(0);

    switch (mProfileId) {
    case ProfileId::kAES_256_GCM:
    case ProfileId::kAES_128_GCM:
        return protectSendControlGCM(packet, seq, encrypted);
    case ProfileId::kAES_128_CM_SHA1_80:
    case ProfileId::kAES_128_CM_SHA1_32:
        return protectSendControlCM(packet, seq, encrypted);
    default:
        assert(false);
        return false;
    }
}

bool SrtpCrypto::protectSendControlGCM(const ByteBuffer& packet, uint32_t seq, ByteBuffer& encrypted)
{
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
    size_t len = 0, total_len = 0;
    bool success = false;
    uint8_t digest[20] = {};

    // Create context with key and iv
    auto context = mSendProvider->createEncryptContext(mSendRtcp.key.data(), iv.data());
    if (!context) {
        goto fail;
    }

    // AAD is the header and the trailer
    if (!context.update_aad(packetData, headerSize)) {
        goto fail;
    }
    if (!context.update_aad(reinterpret_cast<const uint8_t*>(&trailer), trailerSize)) {
        goto fail;
    }

    // Encrypt the body
    if (!context.update_data(encryptedData + headerSize, packetData + headerSize, packetSize - headerSize, &len)) {
        goto fail;
    }
    total_len += len;

    if (!context.finalize(encryptedData + headerSize + len, &len)) {
        goto fail;
    }
    total_len += len;

    // Get tag and copy it after the data
    if (!context.get_digest(digest, kAESGCM_TagSize)) {
        goto fail;
    }
    std::memcpy(encryptedData + encryptedSize - kAESGCM_TagSize - trailerSize, digest, kAESGCM_TagSize);

    // Trailer
    std::memcpy(encryptedData + encryptedSize - trailerSize, &trailer, trailerSize);

    // It all worked
    success = true;
fail:
    if (success) {
        assert(total_len + headerSize + kAESGCM_TagSize + trailerSize == encryptedSize);
        (void)total_len;
        encrypted.resize(encryptedSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::protectSendControlCM(const ByteBuffer& packet, uint32_t seq, ByteBuffer& encrypted)
{
    const size_t digestSize = 10; // for both SHA1_80 and SHA1_32

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
    size_t len = 0, total_len = 0;
    bool success = false;
    uint8_t digest[20] = {};

    // Create context with key and iv
    auto context = mSendProvider->createEncryptContext(mSendRtcp.key.data(), iv.data());
    if (!context) {
        goto fail;
    }

    // Encrypt the body
    if (!context.update_data(encryptedData + headerSize, packetData + headerSize, packetSize - headerSize, &len)) {
        goto fail;
    }
    total_len += len;

    if (!context.finalize(encryptedData + headerSize + len, &len)) {
        goto fail;
    }
    total_len += len;

    // Trailer
    std::memcpy(encryptedData + encryptedSize - trailerSize - digestSize, &trailer, trailerSize);

    // Authentication tag, https://datatracker.ietf.org/doc/html/rfc3711#section-4.2
    if (!mHmacSha1->reset(mSendRtcp.auth.data(), mSendRtcp.auth.size())) {
        goto fail;
    }
    mHmacSha1->update(encryptedData, packetSize + trailerSize);
    mHmacSha1->final(digest);

    std::memcpy(encryptedData + encryptedSize - digestSize, digest, digestSize);

    // It all worked
    success = true;
fail:
    if (success) {
        assert(total_len + headerSize + trailerSize + digestSize == encryptedSize);
        (void)total_len;
        encrypted.resize(encryptedSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::unprotectReceiveControl(const ByteBuffer& packet, ByteBuffer& plain)
{
    plain.resize(0);

    switch (mProfileId) {
    case ProfileId::kAES_256_GCM:
    case ProfileId::kAES_128_GCM:
        return unprotectReceiveControlGCM(packet, plain);
    case ProfileId::kAES_128_CM_SHA1_80:
    case ProfileId::kAES_128_CM_SHA1_32:
        return unprotectReceiveControlCM(packet, plain);
    default:
        assert(false);
        return false;
    }
}

bool SrtpCrypto::unprotectReceiveControlGCM(const ByteBuffer& packet, ByteBuffer& plain)
{
    const auto encryptedSize = packet.size();
    if (encryptedSize < 4 + 4 + 16 + 4) {
        // 4 byte RTCP header
        // 4 byte SSRC
        // ... ciphertext
        // 16 byte AES GCM tag
        // 4 byte SRTCP index
        return false;
    }

    const auto encryptedData = packet.data();

    // Extract ssrc and seq
    // https://datatracker.ietf.org/doc/html/rfc7714#section-9.2
    const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(encryptedData + 4));
    const uint32_t trailer = ntohl(*reinterpret_cast<const uint32_t*>(encryptedData + encryptedSize - 4));
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

    size_t len = 0, plain_len = 0;
    bool success = false;

    // Create context with key and iv
    auto context = mReceiveProvider->createDecryptContext(mReceiveRtcp.key.data(), iv.data());
    if (!context) {
        goto fail;
    }

    // Set tag
    if (!context.set_tag(encryptedData + encryptedSize - kRTCP_TrailerSize - kAESGCM_TagSize, kAESGCM_TagSize)) {
        goto fail;
    }

    // AAD
    if (isEncrypted) {
        if (!context.update_aad(encryptedData, kRTCP_HeaderSize)) {
            goto fail;
        }
    } else {
        if (!context.update_aad(encryptedData, encryptedSize - kAESGCM_TagSize - kRTCP_TrailerSize)) {
            goto fail;
        }
    }

    if (!context.update_aad(encryptedData + encryptedSize - kRTCP_TrailerSize, kRTCP_TrailerSize)) {
        goto fail;
    }

    // The header is not encrypted
    std::memcpy(plainData, encryptedData, kRTCP_HeaderSize);
    plain_len = kRTCP_HeaderSize;

    // Encrypted portion
    if (isEncrypted) {
        if (!context.update_data(plainData + kRTCP_HeaderSize,
                                 encryptedData + kRTCP_HeaderSize,
                                 encryptedSize - kRTCP_HeaderSize - kAESGCM_TagSize - kRTCP_TrailerSize,
                                 &len)) {
            goto fail;
        }
        plain_len += len;
    } else {
        std::memcpy(plainData + kRTCP_HeaderSize,
                    encryptedData + kRTCP_HeaderSize,
                    encryptedSize - kRTCP_HeaderSize - kAESGCM_TagSize - kRTCP_TrailerSize);
        plain_len += encryptedSize - kRTCP_HeaderSize - kAESGCM_TagSize - kRTCP_TrailerSize;
    }

    // Finalize, this validates the GCM auth tag
    if (!context.finalize(plainData + plain_len, &len)) {
        goto fail;
    }
    plain_len += len;

    // It all worked
    success = true;
fail:
    if (success) {
        assert(plain_len == plainSize);
        plain.resize(plainSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::unprotectReceiveControlCM(const ByteBuffer& packet, ByteBuffer& plain)
{
    const size_t digestSize = 10; // for both SHA1_80 and SHA1_32

    const auto encryptedSize = packet.size();
    if (encryptedSize < 4 + 4 + 4 + digestSize) {
        // 4 byte RTCP header
        // 4 byte SSRC
        // ... ciphertext
        // 4 byte SRTCP index
        // 10 byte HMAC-SHA1 digest
        return false;
    }

    const auto encryptedData = packet.data();

    const auto digestPtr = encryptedData + encryptedSize - digestSize;
    uint8_t digest[20] = {}; // 160 bits

    const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(encryptedData + 4));
    const uint32_t trailer = ntohl(*reinterpret_cast<const uint32_t*>(encryptedData + encryptedSize - digestSize - 4));
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

    size_t len = 0, plain_len = 0;
    bool success = false;

    // The header is not encrypted
    std::memcpy(plainData, encryptedData, kRTCP_HeaderSize);
    plain_len = kRTCP_HeaderSize;

    // The main body
    if (isEncrypted) {
        // Create context with key and iv
        auto context = mReceiveProvider->createDecryptContext(mReceiveRtcp.key.data(), iv.data());
        if (!context) {
            goto fail;
        }

        // Decrypt
        if (!context.update_data(plainData + kRTCP_HeaderSize,
                                 encryptedData + kRTCP_HeaderSize,
                                 encryptedSize - kRTCP_HeaderSize - kRTCP_TrailerSize - digestSize,
                                 &len)) {
            goto fail;
        }
        plain_len += len;

        // Finalize
        if (!context.finalize(plainData + plain_len, &len)) {
            goto fail;
        }
        plain_len += len;
    } else {
        std::memcpy(plainData + kRTCP_HeaderSize,
                    encryptedData + kRTCP_HeaderSize,
                    encryptedSize - kRTCP_HeaderSize - kRTCP_TrailerSize - digestSize);
        plain_len += encryptedSize - kRTCP_HeaderSize - kRTCP_TrailerSize - digestSize;
    }

    // Verify the digest
    if (!mHmacSha1->reset(mReceiveRtcp.auth.data(), mReceiveRtcp.auth.size())) {
        goto fail;
    }
    mHmacSha1->update(encryptedData, digestPtr - encryptedData);
    mHmacSha1->final(digest);

    if (!providers::secureEquals(digest, digestPtr, digestSize)) {
        // Digest validation failed
        goto fail;
    }

    // It all worked
    success = true;
fail:
    if (success) {
        assert(plain_len == plainSize);
        plain.resize(plainSize);
        return true;
    }
    return false;
}

bool SrtpCrypto::secureEquals(const void* a, const void* b, size_t size)
{
    return providers::secureEquals(a, b, size);
}

SrtpCrypto::SrtpCrypto(ProfileId profileId,
                       const CryptoVectors& sendRtp,
                       const CryptoVectors& receiveRtp,
                       const CryptoVectors& sendRtcp,
                       const CryptoVectors& receiveRtcp)
    : mProfileId(profileId)
    , mSendRtp(sendRtp)
    , mReceiveRtp(receiveRtp)
    , mSendRtcp(sendRtcp)
    , mReceiveRtcp(receiveRtcp)
    , mSendProvider(std::make_shared<providers::CryptoProvider>(profileId))
    , mReceiveProvider(std::make_shared<providers::CryptoProvider>(profileId))
    , mHmacSha1(std::make_shared<HmacSha1>())
{
}

SrtpCrypto::~SrtpCrypto() = default;

} // namespace srtc

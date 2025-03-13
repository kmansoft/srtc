// Some code in this file is based on Cisco's libSRTP
// https://github.com/cisco/libsrtp
// Copyright (c) 2001-2017 Cisco Systems, Inc.
// All rights reserved.
// Neither the name of the Cisco Systems, Inc. nor the names of its
// contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.

#include "srtc/srtp_connection.h"
#include "srtc/logging.h"

#include "srtp.h"

#include <mutex>

#include <arpa/inet.h>

#include <cassert>
#include <cstring>

#define LOG(level, ...) srtc::log(level, "SrtpConnection", __VA_ARGS__)

namespace {

std::once_flag gSrtpInitFlag;

constexpr size_t kAESGCM_IVSize = 12;
constexpr size_t kAESGCM_TagSize = 16;

constexpr uint32_t kRTCP_EncryptedBit = 0x80000000u;
constexpr size_t kRTCP_HeaderSize = 8u;
constexpr size_t kRTCP_TrailerSize = 4u;

}

namespace srtc {

std::pair<std::shared_ptr<SrtpConnection>, Error> SrtpConnection::create(SSL* dtls_ssl, bool isSetupActive)
{
    // Init the SRTP library
    std::call_once(gSrtpInitFlag, []{
        srtp_init();
    });

    // https://stackoverflow.com/questions/22692109/webrtc-srtp-decryption

    const auto srtpProfileName = SSL_get_selected_srtp_profile(dtls_ssl);
    if (srtpProfileName == nullptr) {
        return { nullptr, { Error::Code::InvalidData, "Cannot get SRTP profile from OpenSSL " }};
    }

    srtp_profile_t srtpProfile;
    size_t srtpKeySize = { 0 }, srtpSaltSize = { 0 };

    switch (srtpProfileName->id) {
        case SRTP_AEAD_AES_256_GCM:
            srtpProfile = srtp_profile_aead_aes_256_gcm;
            srtpKeySize = SRTP_AES_256_KEY_LEN;
            srtpSaltSize = SRTP_AEAD_SALT_LEN;
            break;
        case SRTP_AEAD_AES_128_GCM:
            srtpProfile = srtp_profile_aead_aes_128_gcm;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_AEAD_SALT_LEN;
            break;
        case SRTP_AES128_CM_SHA1_80:
            srtpProfile = srtp_profile_aes128_cm_sha1_80;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_SALT_LEN;
            break;
        case SRTP_AES128_CM_SHA1_32:
            srtpProfile = srtp_profile_aes128_cm_sha1_32;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_SALT_LEN;
            break;
        default:
            return {nullptr, { Error::Code::InvalidData, "Unsupported SRTP profile" }};
    }

    const auto srtpKeyPlusSaltSize = srtpKeySize + srtpSaltSize;
    const auto material = ByteBuffer{srtpKeyPlusSaltSize * 2};

    std::string label = "EXTRACTOR-dtls_srtp";
    SSL_export_keying_material(dtls_ssl,
                               material.data(), srtpKeyPlusSaltSize * 2,
                               label.data(), label.size(),
                               nullptr, 0, 0);

    const auto srtpClientKey = material.data();
    const auto srtpServerKey = srtpClientKey + srtpKeySize;
    const auto srtpClientSalt = srtpServerKey + srtpKeySize;
    const auto srtpServerSalt = srtpClientSalt + srtpSaltSize;

    ByteBuffer srtpClientKeyBuf;
    srtpClientKeyBuf.append(srtpClientKey, srtpKeySize);
    srtpClientKeyBuf.append(srtpClientSalt, srtpSaltSize);

    ByteBuffer srtpServerKeyBuf;
    srtpServerKeyBuf.append(srtpServerKey, srtpKeySize);
    srtpServerKeyBuf.append(srtpServerSalt, srtpSaltSize);

    const auto conn = std::make_shared<SrtpConnection>(
            std::move(srtpClientKeyBuf), std::move(srtpServerKeyBuf),
            srtpKeySize, srtpSaltSize, isSetupActive,
            srtpProfileName->id, srtpProfile);
    return { conn, Error::OK };
}

SrtpConnection::~SrtpConnection()
{
    for (auto& iter : mSrtpInMap) {
        srtp_dealloc(iter.second.srtp);
    }

    for (auto& iter : mSrtpOutMap) {
        srtp_dealloc(iter.second.srtp);
    }
}

size_t SrtpConnection::protectOutgoing(ByteBuffer& packetData)
{
    if (packetData.size() < 4 + 4 + 4) {
        LOG(SRTC_LOG_E, "Outgoing RTP packet is too small");
        return 0;
    }


    ChannelKey key = { };
    key.ssrc = ntohl(*reinterpret_cast<const uint32_t*>(packetData.data() + 8));
    key.payloadId = packetData.data()[1] & 0x7F;

    const auto& channelValue = ensureSrtpChannel(mSrtpOutMap, key, &mSrtpSendPolicy, 0);

    auto size_1 = packetData.size();

    packetData.padding(SRTP_MAX_TRAILER_LEN);

    auto data = packetData.data();
    auto size_2 = packetData.size();

    const auto result = srtp_protect(channelValue.srtp,
                                     data, size_1,
                                     data, &size_2,
                                     0);
    if (result != srtp_err_status_ok) {
        LOG(SRTC_LOG_E, "srtp_protect() failed: %d", result);
        return 0;
    }

    assert(size_2 > size_1);
    return size_2;
}

size_t SrtpConnection::unprotectIncomingControl(ByteBuffer& packetData)
{
    if (packetData.size() < 4 + 4 + 4) {
        LOG(SRTC_LOG_E, "Incoming RTCP packet is too small");
        return 0;
    }

    ChannelKey key = { };
    key.ssrc = ntohl(*reinterpret_cast<const uint32_t*>(packetData.data() + 4));
    key.payloadId = 0;

    auto& channelValue = ensureSrtpChannel(mSrtpInMap, key, &mSrtpReceivePolicy,
                                                 std::numeric_limits<uint32_t>::max());

    ByteBuffer plain;

    if (mProfileT == SRTP_AEAD_AES_256_GCM || mProfileT == SRTP_AEAD_AES_128_GCM) {
        // Test code
        const auto unprotectedSize = unprotectIncomingControlAESGCM(channelValue, packetData, plain);
        assert(unprotectedSize == 0 || unprotectedSize == plain.capacity());
        plain.resize(unprotectedSize);
    }

    auto data = packetData.data();
    auto size1 = packetData.size();
    auto size2 = size1;
    const auto status = srtp_unprotect_rtcp(channelValue.srtp,
                                            data, size1,
                                            data, &size2);

    if (status != srtp_err_status_ok) {
        LOG(SRTC_LOG_E, "srtp_unprotect_rtcp() failed: %d", status);
        return 0;
    }

    return size2;
}

size_t SrtpConnection::unprotectIncomingControlAESGCM(ChannelValue& channelValue,
                                                      const ByteBuffer& encrypted,
                                                      ByteBuffer& plain)
{
    const auto encryptedSize = encrypted.size();
    if (encryptedSize <= 4 + 4 + 16 + 4) {
        // 4 byte RTCP header
        // 4 byte SSRC
        // ... ciphertext
        // 16 byte AES GCM tag
        // 4 byte SRTCP index
        return 0;
    }

    const auto encrypedData = encrypted.data();

    // Extract ssrc and seq
    // https://datatracker.ietf.org/doc/html/rfc7714#section-9.2
    const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(encrypedData + 4));
    const uint32_t trailer = ntohl(*reinterpret_cast<const uint32_t*>(encrypedData + encryptedSize - 4));
    const uint32_t seq = ~kRTCP_EncryptedBit & trailer;
    const auto isEncrypted = (kRTCP_EncryptedBit & trailer) != 0;

    // Compute the IV
    // https://datatracker.ietf.org/doc/html/rfc7714#section-9.1
    CryptoBytes iv;
    CryptoBytesWriter ivw(iv);
    ivw.writeU8(0);
    ivw.writeU8(0);
    ivw.writeU32(ssrc);
    ivw.writeU8(0);
    ivw.writeU8(0);
    ivw.writeU32(seq);
    iv ^= mReceiveMasterSalt;

    auto ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return 0;
    }

    EVP_CIPHER_CTX_set_padding(ctx, 0);

    auto cipher = mProfileT == SRTP_AEAD_AES_256_GCM ? EVP_aes_256_gcm() : EVP_aes_128_gcm();;
    if (!EVP_DecryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr)) {
        return 0;
    }

    // Set key and iv
    if (!EVP_DecryptInit_ex(ctx, nullptr, nullptr, mReceiveMasterKey.data(), iv.data())) {
        return 0;
    }

    // AAD
    int len = 0, plain_len = 0;
    if (isEncrypted) {
        if (!EVP_DecryptUpdate(ctx, nullptr, &len, encrypedData, kRTCP_HeaderSize)) {
            return 0;
        }
    } else {
        if (!EVP_DecryptUpdate(ctx, nullptr, &len, encrypedData,
                               static_cast<int>(encryptedSize - kAESGCM_TagSize - kRTCP_TrailerSize))) {
            return 0;
        }
    }

    if (!EVP_DecryptUpdate(ctx, nullptr, &len, encrypedData + encryptedSize - kRTCP_TrailerSize, kRTCP_TrailerSize)) {
        return 0;
    }

    // Output buffer
    const auto plainSize = encryptedSize - kAESGCM_TagSize - kRTCP_TrailerSize;
    plain.reserve(plainSize);

    const auto plainData = plain.data();

    // The header is not encrypted
    std::memcpy(plainData, encrypedData, kRTCP_HeaderSize);

    // Encrypted portion
    if (isEncrypted) {
        if (!EVP_DecryptUpdate(ctx, plainData + kRTCP_HeaderSize, &len, encrypedData + kRTCP_HeaderSize,
                               static_cast<int>(encryptedSize - kRTCP_HeaderSize - kAESGCM_TagSize - kRTCP_TrailerSize))) {
            return 0;
        }
        plain_len = len;
    } else {
        std::memcpy(plainData + kRTCP_HeaderSize, encrypedData + kRTCP_HeaderSize,
                    encryptedSize - kRTCP_HeaderSize - kAESGCM_TagSize - kRTCP_TrailerSize);
        plain_len = static_cast<int>(encryptedSize - kRTCP_HeaderSize - kAESGCM_TagSize - kRTCP_TrailerSize);
    }

    // The tag
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kAESGCM_TagSize,
                             encrypedData + encryptedSize - kRTCP_TrailerSize - kAESGCM_TagSize)) {
        return 0;
    }

    // Finalize
    int final_ret = EVP_DecryptFinal_ex(ctx, plainData + plain_len, &len);
    if (final_ret > 0) {
        plain_len += len;
    }

    // Cleanup
    EVP_CIPHER_CTX_free(ctx);

    if (final_ret > 0) {
        assert(plain_len == plainSize);
        return plainSize;
    } else {
        return 0;
    }
}

SrtpConnection::SrtpConnection(ByteBuffer&& srtpClientKeyBuf,
                               ByteBuffer&& srtpServerKeyBuf,
                               size_t keySize,
                               size_t saltSize,
                               bool isSetupActive,
                               unsigned long profileId,
                               srtp_profile_t profileT)
    : mSrtpClientKeyBuf(std::move(srtpClientKeyBuf))
    , mSrtpServerKeyBuf(std::move(srtpServerKeyBuf))
    , mKeySize(keySize)
    , mSaltSize(saltSize)
    , mIsSetupActive(isSetupActive)
    , mProfileId(profileId)
    , mProfileT(profileT)
{
    // Receive policy
    std::memset(&mSrtpReceivePolicy, 0, sizeof(mSrtpReceivePolicy));
    mSrtpReceivePolicy.ssrc.type = ssrc_any_inbound;
    mSrtpReceivePolicy.key = mIsSetupActive ? mSrtpClientKeyBuf.data() : mSrtpServerKeyBuf.data();
    mSrtpReceivePolicy.allow_repeat_tx = false;

    srtp_crypto_policy_set_from_profile_for_rtp(&mSrtpReceivePolicy.rtp, mProfileT);
    srtp_crypto_policy_set_from_profile_for_rtcp(&mSrtpReceivePolicy.rtcp, mProfileT);

    // Receive key and salt
    if (mIsSetupActive) {
        mReceiveMasterKey.assign(mSrtpClientKeyBuf.data(), mKeySize);
        mReceiveMasterSalt.assign(mSrtpClientKeyBuf.data() + mKeySize, mSaltSize);
    } else {
        mReceiveMasterKey.assign(mSrtpServerKeyBuf.data(), mKeySize);
        mReceiveMasterSalt.assign(mSrtpServerKeyBuf.data() + mKeySize, mSaltSize);
    }

    // Send policy
    std::memset(&mSrtpSendPolicy, 0, sizeof(mSrtpSendPolicy));
    mSrtpSendPolicy.ssrc.type = ssrc_any_outbound;
    mSrtpSendPolicy.key = mIsSetupActive ? mSrtpServerKeyBuf.data() : mSrtpClientKeyBuf.data();
    mSrtpSendPolicy.allow_repeat_tx = true;

    srtp_crypto_policy_set_from_profile_for_rtp(&mSrtpSendPolicy.rtp, mProfileT);
    srtp_crypto_policy_set_from_profile_for_rtcp(&mSrtpSendPolicy.rtcp, mProfileT);
}

SrtpConnection::ChannelValue& SrtpConnection::ensureSrtpChannel(ChannelMap& map,
                                                                const ChannelKey& key,
                                                                const srtp_policy_t* policy,
                                                                uint32_t maxPossibleValueForReplayProtection)
{
    const auto iter = map.find(key);
    if (iter != map.end()) {
        return iter->second;
    }

    srtp_t srtp = nullptr;
    srtp_create(&srtp, policy);

    auto replayProtection = maxPossibleValueForReplayProtection != 0
            ? std::make_unique<ReplayProtection>(maxPossibleValueForReplayProtection, 2048)
                    : nullptr;
    const auto result = map.insert({ key,
                                    ChannelValue{ srtp, std::move(replayProtection) } });
    return result.first->second;
}

}

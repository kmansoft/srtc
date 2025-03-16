// Some code in this file is based on Cisco's libSRTP
// https://github.com/cisco/libsrtp
// Copyright (c) 2001-2017 Cisco Systems, Inc.
// All rights reserved.
// Neither the name of the Cisco Systems, Inc. nor the names of its
// contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.

#include "srtc/srtp_connection.h"
#include "srtc/srtp_crypto.h"
#include "srtc/logging.h"

#include "srtp.h"

#include <mutex>

#include <arpa/inet.h>

#include <openssl/ssl.h>

#include <cassert>
#include <cstring>

#define LOG(level, ...) srtc::log(level, "SrtpConnection", __VA_ARGS__)

namespace {

std::once_flag gSrtpInitFlag;

}

namespace srtc {

const char* const SrtpConnection::kSrtpCipherList =
        "SRTP_AEAD_AES_128_GCM:SRTP_AEAD_AES_256_GCM:SRTP_AES128_CM_SHA1_80:SRTP_AES128_CM_SHA1_32";

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

    LOG(SRTC_LOG_V, "Connected with %s cipher", srtpProfileName->name);

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

    CryptoBytes receiveMasterKey, receiveMasterSalt;

    if (isSetupActive) {
        receiveMasterKey.assign(srtpClientKey, srtpKeySize);
        receiveMasterSalt.assign(srtpClientSalt, srtpSaltSize);
    } else {
        receiveMasterKey.assign(srtpServerKey, srtpKeySize);
        receiveMasterSalt.assign(srtpServerSalt, srtpSaltSize);
    }

    const auto [crypto, error] = SrtpCrypto::create(srtpProfileName->id,
                                                    receiveMasterKey, receiveMasterSalt);
    if (error.isError()) {
        return { nullptr, error };
    }

    ByteBuffer srtpClientKeyBuf;
    srtpClientKeyBuf.append(srtpClientKey, srtpKeySize);
    srtpClientKeyBuf.append(srtpClientSalt, srtpSaltSize);

    ByteBuffer srtpServerKeyBuf;
    srtpServerKeyBuf.append(srtpServerKey, srtpKeySize);
    srtpServerKeyBuf.append(srtpServerSalt, srtpSaltSize);

    const auto conn = std::make_shared<SrtpConnection>(
            std::move(srtpClientKeyBuf), std::move(srtpServerKeyBuf),
            srtpKeySize, srtpSaltSize,
            crypto,
            isSetupActive,
            srtpProfile);
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
        const auto unprotectedSize = mCrypto->unprotectReceiveRtcp(packetData, plain);
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

    if (mProfileT == SRTP_AEAD_AES_256_GCM || mProfileT == SRTP_AEAD_AES_128_GCM) {
        assert(plain.size() == size2);
        assert(std::memcmp(plain.data(), data, size2) == 0);
    }

    return size2;
}

SrtpConnection::SrtpConnection(ByteBuffer&& srtpClientKeyBuf,
                               ByteBuffer&& srtpServerKeyBuf,
                               size_t keySize,
                               size_t saltSize,
                               const std::shared_ptr<SrtpCrypto>& crypto,
                               bool isSetupActive,
                               srtp_profile_t profileT)
    : mSrtpClientKeyBuf(std::move(srtpClientKeyBuf))
    , mSrtpServerKeyBuf(std::move(srtpServerKeyBuf))
    , mKeySize(keySize)
    , mSaltSize(saltSize)
    , mCrypto(crypto)
    , mIsSetupActive(isSetupActive)
    , mProfileT(profileT)
{
    // Receive policy
    std::memset(&mSrtpReceivePolicy, 0, sizeof(mSrtpReceivePolicy));
    mSrtpReceivePolicy.ssrc.type = ssrc_any_inbound;
    mSrtpReceivePolicy.key = mIsSetupActive ? mSrtpClientKeyBuf.data() : mSrtpServerKeyBuf.data();
    mSrtpReceivePolicy.allow_repeat_tx = false;

    srtp_crypto_policy_set_from_profile_for_rtp(&mSrtpReceivePolicy.rtp, mProfileT);
    srtp_crypto_policy_set_from_profile_for_rtcp(&mSrtpReceivePolicy.rtcp, mProfileT);

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

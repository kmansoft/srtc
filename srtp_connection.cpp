#include "srtc/srtp_connection.h"
#include "srtc/logging.h"

#include "srtp.h"

#include <mutex>
#include <arpa/inet.h>
#include <cassert>

#define LOG(level, ...) srtc::log(level, "SrtpConnection", __VA_ARGS__)

namespace {

std::once_flag gSrtpInitFlag;

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
            srtpKeySize, isSetupActive, srtpProfile);
    return { conn, Error::OK };
}

SrtpConnection::~SrtpConnection()
{
    for (auto& iter : mSrtpInMap) {
        srtp_dealloc(iter.second);
    }

    for (auto& iter : mSrtpOutMap) {
        srtp_dealloc(iter.second);
    }
}

size_t SrtpConnection::protectOutgoing(ByteBuffer& packetData)
{
    if (packetData.size() < 4 + 4 + 4) {
        LOG(SRTC_LOG_E, "Outgoing RTP packet is too small");
        return 0;
    }


    ChannelKey key;
    key.ssrc = ntohl(*reinterpret_cast<const uint32_t*>(packetData.data() + 8));
    key.payloadId = packetData.data()[1] & 0x7F;

    const auto srtp = ensureSrtpChannel(mSrtpOutMap, key, &mSrtpSendPolicy);

    int rtp_size_1 = static_cast<int>(packetData.size());
    int rtp_size_2 = rtp_size_1;

    packetData.padding(SRTP_MAX_TRAILER_LEN);

    const auto result = srtp_protect(srtp, packetData.data(), &rtp_size_2);
    if (result != srtp_err_status_ok) {
        LOG(SRTC_LOG_E, "srtp_protect() failed: %d", result);
        return 0;
    }

    assert(rtp_size_2 > rtp_size_1);
    return static_cast<size_t>(rtp_size_2);
}

size_t SrtpConnection::unprotectIncomingControl(ByteBuffer& packetData)
{
    if (packetData.size() < 4 + 4) {
        LOG(SRTC_LOG_E, "Incoming RTCP packet is too small");
        return 0;
    }

    ChannelKey key;
    key.ssrc = ntohl(*reinterpret_cast<const uint32_t*>(packetData.data() + 4));
    key.payloadId = 0;

    const auto srtp = ensureSrtpChannel(mSrtpInMap, key, &mSrtpReceivePolicy);

    int rtcpSize = static_cast<int>(packetData.size());
    const auto status = srtp_unprotect_rtcp(srtp, packetData.data(), &rtcpSize);

    if (status != srtp_err_status_ok) {
        LOG(SRTC_LOG_E, "srtp_unprotect_rtcp() failed: %d", status);
        return 0;
    }

    return static_cast<size_t>(rtcpSize);
}

SrtpConnection::SrtpConnection(ByteBuffer&& srtpClientKeyBuf,
                               ByteBuffer&& srtpServerKeyBuf,
                               size_t keySize,
                               bool isSetupActive,
                               srtp_profile_t profile)
    : mSrtpClientKeyBuf(std::move(srtpClientKeyBuf))
    , mSrtpServerKeyBuf(std::move(srtpServerKeyBuf))
    , mKeySize(keySize)
    , mIsSetupActive(isSetupActive)
    , mProfile(profile)
{
    // Receive policy
    mSrtpReceivePolicy.ssrc.type = ssrc_any_inbound;
    mSrtpReceivePolicy.key = mIsSetupActive ? mSrtpClientKeyBuf.data() : mSrtpServerKeyBuf.data();
    mSrtpReceivePolicy.allow_repeat_tx = true;

    srtp_crypto_policy_set_from_profile_for_rtp(&mSrtpReceivePolicy.rtp, mProfile);
    srtp_crypto_policy_set_from_profile_for_rtcp(&mSrtpReceivePolicy.rtcp, mProfile);

    // Send policy
    mSrtpSendPolicy.ssrc.type = ssrc_any_inbound;
    mSrtpSendPolicy.key = mIsSetupActive ? mSrtpServerKeyBuf.data() : mSrtpClientKeyBuf.data();
    mSrtpSendPolicy.allow_repeat_tx = true;

    srtp_crypto_policy_set_from_profile_for_rtp(&mSrtpSendPolicy.rtp, mProfile);
    srtp_crypto_policy_set_from_profile_for_rtcp(&mSrtpSendPolicy.rtcp, mProfile);
}

srtp_t SrtpConnection::ensureSrtpChannel(ChannelMap& map,
                                         const ChannelKey& key,
                                         const srtp_policy_t* policy)
{
    const auto iter = map.find(key);
    if (iter != map.end()) {
        return iter->second;
    }

    srtp_t srtp = nullptr;
    srtp_create(&srtp, policy);
    map.insert({ key, srtp });
    return srtp;
}

}

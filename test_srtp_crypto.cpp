#include <gtest/gtest.h>

#include "srtc/byte_buffer.h"
#include "srtc/rtcp_packet.h"
#include "srtc/rtp_extension.h"
#include "srtc/rtp_packet.h"
#include "srtc/srtp_crypto.h"
#include "srtc/srtp_openssl.h"
#include "srtc/srtp_util.h"
#include "srtc/track.h"

#include <cstring>
#include <ctime>
#include <mutex>

#include <openssl/rand.h>
#include <openssl/srtp.h>
#include <srtp.h>

// Init Cisco's libSRTP

namespace
{

std::once_flag gInitFlag;

void initLibSRTP()
{
    std::call_once(gInitFlag, [] {
        srtp_init();

        struct timespec t = {};
        clock_gettime(CLOCK_REALTIME, &t);
        srand48(t.tv_nsec);
    });
}

} // namespace

// RTP send

TEST(SrtpCrypto, RtpSend)
{
    std::cout << "SrtpCrypto RtpSend" << std::endl;

    initLibSRTP();
    srtc::initOpenSSL();

    static const uint16_t kOpenSslProfileList[] = {
        SRTP_AEAD_AES_256_GCM, SRTP_AEAD_AES_128_GCM, SRTP_AES128_CM_SHA1_80, SRTP_AES128_CM_SHA1_32
    };

    for (const auto openSSlProfile : kOpenSslProfileList) {
        const char* srtpProfileName;
        srtp_profile_t srtpProfileId;
        size_t srtpKeySize = { 0 }, srtpSaltSize = { 0 };

        switch (openSSlProfile) {
        case SRTP_AEAD_AES_256_GCM:
            srtpProfileName = "SRTP_AEAD_AES_256_GCM";
            srtpProfileId = srtp_profile_aead_aes_256_gcm;
            srtpKeySize = SRTP_AES_256_KEY_LEN;
            srtpSaltSize = SRTP_AEAD_SALT_LEN;
            break;
        case SRTP_AEAD_AES_128_GCM:
            srtpProfileName = "SRTP_AEAD_AES_128_GCM";
            srtpProfileId = srtp_profile_aead_aes_128_gcm;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_AEAD_SALT_LEN;
            break;
        case SRTP_AES128_CM_SHA1_80:
            srtpProfileName = "SRTP_AES128_CM_SHA1_80";
            srtpProfileId = srtp_profile_aes128_cm_sha1_80;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_SALT_LEN;
            break;
        case SRTP_AES128_CM_SHA1_32:
            srtpProfileName = "SRTP_AES128_CM_SHA1_32";
            srtpProfileId = srtp_profile_aes128_cm_sha1_32;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_SALT_LEN;
            break;
        default:
            ASSERT_TRUE(false);
            break;
        }

        std::cout << "Testing " << srtpProfileName << std::endl;

        // Generate random keys and salts
        uint8_t bufSendMasterKey[32], bufSendMasterSalt[32];
        RAND_bytes(bufSendMasterKey, sizeof(bufSendMasterKey));
        RAND_bytes(bufSendMasterSalt, sizeof(bufSendMasterSalt));

        uint8_t bufReceiveMasterKey[32], bufReceiveMasterSalt[32];
        RAND_bytes(bufReceiveMasterKey, sizeof(bufReceiveMasterKey));
        RAND_bytes(bufReceiveMasterSalt, sizeof(bufReceiveMasterSalt));

        // Convert to our objects
        srtc::CryptoBytes sendMasterKey, sendMasterSalt;
        sendMasterKey.assign(bufSendMasterKey, srtpKeySize);
        sendMasterSalt.assign(bufSendMasterSalt, srtpSaltSize);

        srtc::CryptoBytes receiveMasterKey, receiveMasterSalt;
        receiveMasterKey.assign(bufReceiveMasterKey, srtpKeySize);
        receiveMasterSalt.assign(bufReceiveMasterSalt, srtpSaltSize);

        // Create our own crypto
        const auto [crypto, error] = srtc::SrtpCrypto::create(
            openSSlProfile, sendMasterKey, sendMasterSalt, receiveMasterKey, receiveMasterSalt);
        ASSERT_TRUE(error.isOk());
        ASSERT_TRUE(crypto);

        // Create libSRTP crypto
        srtc::ByteBuffer bufMasterCombined;
        bufMasterCombined.append(bufSendMasterKey, srtpKeySize);
        bufMasterCombined.append(bufSendMasterSalt, srtpSaltSize);

        srtp_policy_t srtpPolicy;

        std::memset(&srtpPolicy, 0, sizeof(srtpPolicy));
        srtpPolicy.ssrc.type = ssrc_any_outbound;
        srtpPolicy.key = bufMasterCombined.data();
        srtpPolicy.allow_repeat_tx = true;

        srtp_crypto_policy_set_from_profile_for_rtp(&srtpPolicy.rtp, srtpProfileId);
        srtp_crypto_policy_set_from_profile_for_rtcp(&srtpPolicy.rtcp, srtpProfileId);

        srtp_t srtp = nullptr;
        ASSERT_EQ(srtp_create(&srtp, &srtpPolicy), srtp_err_status_ok);

        // Randomly generate RTP packets, encrypt them using libSRTP and our crypto.
        // Verify that the result of encryption is the same for both.
        uint32_t ssrc = 0x12345678;
        uint16_t sequence = 65000;
        uint32_t rolloverCounter = 0;
        uint32_t timestamp = 10000;

        std::optional<uint16_t> prevSequence;

        const auto track = std::make_shared<srtc::Track>(
            0, srtc::MediaType::Video, "0", ssrc, 96, 0, 0, srtc::Codec::H264, nullptr, nullptr, 90000, false, false);

        {
            // Edge case 1: empty payload with an extension, not valid in our library
            uint8_t extensionData[15];
            RAND_bytes(extensionData, sizeof(extensionData));
            srtc::RtpExtension extension = { 1, { extensionData, sizeof(extensionData) } };

            srtc::ByteBuffer payload; // empty

            const auto packet =
                std::make_shared<srtc::RtpPacket>(track, false, 0, 100, 1000, std::move(extension), std::move(payload));
            const auto output = packet->generate();

            srtc::ByteBuffer protectedSrtcCrypto;
            ASSERT_FALSE(crypto->protectSendRtp(output.buf, output.rollover, protectedSrtcCrypto));
        }

        {
            // Edge case 2: 1 byte payload with an extension, valid in our library
            uint8_t extensionData[15];
            RAND_bytes(extensionData, sizeof(extensionData));
            srtc::RtpExtension extension = { 1, { extensionData, sizeof(extensionData) } };

            uint8_t payloadData[1];
            RAND_bytes(payloadData, sizeof(payloadData));

            srtc::ByteBuffer payload = { payloadData, sizeof(payloadData) };

            const auto packet =
                std::make_shared<srtc::RtpPacket>(track, false, 0, 100, 1000, std::move(extension), std::move(payload));
            const auto output = packet->generate();

            srtc::ByteBuffer protectedSrtcCrypto;
            ASSERT_TRUE(crypto->protectSendRtp(output.buf, output.rollover, protectedSrtcCrypto));
        }

        for (auto repeatIndex = 0; repeatIndex < 5000; repeatIndex += 1) {
            const auto payloadSize = 5 + lrand48() % 1000;
            srtc::ByteBuffer payload(payloadSize);
            RAND_bytes(payload.data(), static_cast<int>(payload.capacity()));
            payload.resize(payloadSize);

            // Rollover counter
            if (prevSequence.has_value() && prevSequence > sequence) {
                rolloverCounter += 1;
            }
            prevSequence = sequence;

            srtc::RtpExtension extension;

            if ((repeatIndex % 2) == 1) {
                // Extension
                const auto extensionId = static_cast<uint16_t>(1 + lrand48() % 2000);
                const auto extensionLen = static_cast<size_t>(1 + lrand48() % 200);
                srtc::ByteBuffer extensionData(extensionLen);
                extensionData.resize(extensionLen);
                RAND_bytes(extensionData.data(), static_cast<int>(extensionLen));

                extension = { extensionId, std::move(extensionData) };
            }

            const auto packet = std::make_shared<srtc::RtpPacket>(
                track, false, rolloverCounter, sequence, timestamp, std::move(extension), std::move(payload));
            // This is our packet's unencrypted data
            const auto source = packet->generate();

            // Encrypt using libSRTP
            srtc::ByteBuffer protectedLibSrtp(source.buf.size() + SRTP_MAX_TRAILER_LEN);
            size_t protectedSize = protectedLibSrtp.capacity();
            ASSERT_EQ(
                srtp_protect(srtp, source.buf.data(), source.buf.size(), protectedLibSrtp.data(), &protectedSize, 0),
                srtp_err_status_ok);
            protectedLibSrtp.resize(protectedSize);

            // Encrypt using our own crypto
            srtc::ByteBuffer protectedSrtcCrypto;
            ASSERT_TRUE(crypto->protectSendRtp(source.buf, source.rollover, protectedSrtcCrypto));

            // Validate
            ASSERT_EQ(protectedLibSrtp.size(), protectedSrtcCrypto.size());

            for (size_t i = 0; i < protectedSize; i += 1) {
                ASSERT_EQ(protectedLibSrtp.data()[i], protectedSrtcCrypto.data()[i])
                    << " diff at offset " << i << " of " << protectedLibSrtp.size() << srtpProfileName << std::endl;
            }

            // Advance
            sequence += 1;
            timestamp += 1723;
        }
    }
}

// RTCP send

TEST(SrtpCrypto, RtcpSend)
{
    std::cout << "SrtpCrypto RtcpSend" << std::endl;

    initLibSRTP();
    srtc::initOpenSSL();

    static const uint16_t kOpenSslProfileList[] = {
        SRTP_AEAD_AES_256_GCM, SRTP_AEAD_AES_128_GCM, SRTP_AES128_CM_SHA1_80, SRTP_AES128_CM_SHA1_32
    };

    for (const auto openSSlProfile : kOpenSslProfileList) {
        const char* srtpProfileName;
        srtp_profile_t srtpProfileId;
        size_t srtpKeySize = { 0 }, srtpSaltSize = { 0 };

        switch (openSSlProfile) {
        case SRTP_AEAD_AES_256_GCM:
            srtpProfileName = "SRTP_AEAD_AES_256_GCM";
            srtpProfileId = srtp_profile_aead_aes_256_gcm;
            srtpKeySize = SRTP_AES_256_KEY_LEN;
            srtpSaltSize = SRTP_AEAD_SALT_LEN;
            break;
        case SRTP_AEAD_AES_128_GCM:
            srtpProfileName = "SRTP_AEAD_AES_128_GCM";
            srtpProfileId = srtp_profile_aead_aes_128_gcm;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_AEAD_SALT_LEN;
            break;
        case SRTP_AES128_CM_SHA1_80:
            srtpProfileName = "SRTP_AES128_CM_SHA1_80";
            srtpProfileId = srtp_profile_aes128_cm_sha1_80;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_SALT_LEN;
            break;
        case SRTP_AES128_CM_SHA1_32:
            srtpProfileName = "SRTP_AES128_CM_SHA1_32";
            srtpProfileId = srtp_profile_aes128_cm_sha1_32;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_SALT_LEN;
            break;
        default:
            ASSERT_TRUE(false);
            break;
        }

        std::cout << "Testing " << srtpProfileName << std::endl;

        // Generate random keys and salts
        uint8_t bufSendMasterKey[32], bufSendMasterSalt[32];
        RAND_bytes(bufSendMasterKey, sizeof(bufSendMasterKey));
        RAND_bytes(bufSendMasterSalt, sizeof(bufSendMasterSalt));

        uint8_t bufReceiveMasterKey[32], bufReceiveMasterSalt[32];
        RAND_bytes(bufReceiveMasterKey, sizeof(bufReceiveMasterKey));
        RAND_bytes(bufReceiveMasterSalt, sizeof(bufReceiveMasterSalt));

        // Convert to our objects
        srtc::CryptoBytes sendMasterKey, sendMasterSalt;
        sendMasterKey.assign(bufSendMasterKey, srtpKeySize);
        sendMasterSalt.assign(bufSendMasterSalt, srtpSaltSize);

        srtc::CryptoBytes receiveMasterKey, receiveMasterSalt;
        receiveMasterKey.assign(bufReceiveMasterKey, srtpKeySize);
        receiveMasterSalt.assign(bufReceiveMasterSalt, srtpSaltSize);

        // Create our own crypto
        const auto [crypto, error] = srtc::SrtpCrypto::create(
            openSSlProfile, sendMasterKey, sendMasterSalt, receiveMasterKey, receiveMasterSalt);
        ASSERT_TRUE(error.isOk());
        ASSERT_TRUE(crypto);

        // Create libSRTP crypto
        srtc::ByteBuffer bufMasterCombined;
        bufMasterCombined.append(bufSendMasterKey, srtpKeySize);
        bufMasterCombined.append(bufSendMasterSalt, srtpSaltSize);

        srtp_policy_t srtpPolicy;

        std::memset(&srtpPolicy, 0, sizeof(srtpPolicy));
        srtpPolicy.ssrc.type = ssrc_any_outbound;
        srtpPolicy.key = bufMasterCombined.data();
        srtpPolicy.allow_repeat_tx = true;

        srtp_crypto_policy_set_from_profile_for_rtp(&srtpPolicy.rtp, srtpProfileId);
        srtp_crypto_policy_set_from_profile_for_rtcp(&srtpPolicy.rtcp, srtpProfileId);

        srtp_t srtp = nullptr;
        ASSERT_EQ(srtp_create(&srtp, &srtpPolicy), srtp_err_status_ok);

        // Randomly generate RTCP packets, encrypt them using libSRTP and our crypto.
        // Verify that the result of encryption is the same for both.
        uint32_t ssrc = 0x12345678;
        uint32_t sequence = 1;

        const auto track = std::make_shared<srtc::Track>(
            0, srtc::MediaType::Video, "0", ssrc, 96, 0, 0, srtc::Codec::H264, nullptr, nullptr, 90000, false, false);

        for (auto repeatIndex = 0; repeatIndex < 5000; repeatIndex += 1) {
            const auto payloadSize = 5 + lrand48() % 1000;
            srtc::ByteBuffer payload(payloadSize);
            RAND_bytes(payload.data(), static_cast<int>(payload.capacity()));
            payload.resize(payloadSize);

            const auto packet = std::make_shared<srtc::RtcpPacket>(track, 0x11, 0x22, std::move(payload));
            // This is our packet's unencrypted data
            const auto source = packet->generate();

            // Encrypt using libSRTP
            srtc::ByteBuffer protectedLibSrtp(source.size() + SRTP_MAX_TRAILER_LEN);
            size_t protectedSize = protectedLibSrtp.capacity();
            ASSERT_EQ(srtp_protect_rtcp(srtp, source.data(), source.size(), protectedLibSrtp.data(), &protectedSize, 0),
                      srtp_err_status_ok);
            protectedLibSrtp.resize(protectedSize);

            // Encrypt using our own crypto
            srtc::ByteBuffer protectedSrtcCrypto;
            ASSERT_TRUE(crypto->protectSendRtcp(source, sequence, protectedSrtcCrypto));

            // Validate
            ASSERT_EQ(protectedLibSrtp.size(), protectedSrtcCrypto.size());

            for (size_t i = 0; i < protectedSize; i += 1) {
                ASSERT_EQ(protectedLibSrtp.data()[i], protectedSrtcCrypto.data()[i])
                    << " diff at offset " << i << " of " << protectedLibSrtp.size() << " " << srtpProfileName
                    << std::endl;
            }

            // Advance
            sequence += 1;
        }
    }
}

// RTCP send - multiple RTCP packets in one UDP packet

TEST(SrtpCrypto, RtcpSendMulti)
{
    std::cout << "SrtpCrypto RtcpSendMulti" << std::endl;

    initLibSRTP();
    srtc::initOpenSSL();

    static const uint16_t kOpenSslProfileList[] = {
        SRTP_AEAD_AES_256_GCM, SRTP_AEAD_AES_128_GCM, SRTP_AES128_CM_SHA1_80, SRTP_AES128_CM_SHA1_32
    };

    for (const auto openSSlProfile : kOpenSslProfileList) {
        const char* srtpProfileName;
        srtp_profile_t srtpProfileId;
        size_t srtpKeySize = { 0 }, srtpSaltSize = { 0 };

        switch (openSSlProfile) {
        case SRTP_AEAD_AES_256_GCM:
            srtpProfileName = "SRTP_AEAD_AES_256_GCM";
            srtpProfileId = srtp_profile_aead_aes_256_gcm;
            srtpKeySize = SRTP_AES_256_KEY_LEN;
            srtpSaltSize = SRTP_AEAD_SALT_LEN;
            break;
        case SRTP_AEAD_AES_128_GCM:
            srtpProfileName = "SRTP_AEAD_AES_128_GCM";
            srtpProfileId = srtp_profile_aead_aes_128_gcm;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_AEAD_SALT_LEN;
            break;
        case SRTP_AES128_CM_SHA1_80:
            srtpProfileName = "SRTP_AES128_CM_SHA1_80";
            srtpProfileId = srtp_profile_aes128_cm_sha1_80;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_SALT_LEN;
            break;
        case SRTP_AES128_CM_SHA1_32:
            srtpProfileName = "SRTP_AES128_CM_SHA1_32";
            srtpProfileId = srtp_profile_aes128_cm_sha1_32;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_SALT_LEN;
            break;
        default:
            ASSERT_TRUE(false);
            break;
        }

        std::cout << "Testing " << srtpProfileName << std::endl;

        // Generate random keys and salts
        uint8_t bufSendMasterKey[32], bufSendMasterSalt[32];
        RAND_bytes(bufSendMasterKey, sizeof(bufSendMasterKey));
        RAND_bytes(bufSendMasterSalt, sizeof(bufSendMasterSalt));

        uint8_t bufReceiveMasterKey[32], bufReceiveMasterSalt[32];
        RAND_bytes(bufReceiveMasterKey, sizeof(bufReceiveMasterKey));
        RAND_bytes(bufReceiveMasterSalt, sizeof(bufReceiveMasterSalt));

        // Convert to our objects
        srtc::CryptoBytes sendMasterKey, sendMasterSalt;
        sendMasterKey.assign(bufSendMasterKey, srtpKeySize);
        sendMasterSalt.assign(bufSendMasterSalt, srtpSaltSize);

        srtc::CryptoBytes receiveMasterKey, receiveMasterSalt;
        receiveMasterKey.assign(bufReceiveMasterKey, srtpKeySize);
        receiveMasterSalt.assign(bufReceiveMasterSalt, srtpSaltSize);

        // Create our own crypto
        const auto [crypto, error] = srtc::SrtpCrypto::create(
            openSSlProfile, sendMasterKey, sendMasterSalt, receiveMasterKey, receiveMasterSalt);
        ASSERT_TRUE(error.isOk());
        ASSERT_TRUE(crypto);

        // Create libSRTP crypto
        srtc::ByteBuffer bufMasterCombined;
        bufMasterCombined.append(bufSendMasterKey, srtpKeySize);
        bufMasterCombined.append(bufSendMasterSalt, srtpSaltSize);

        srtp_policy_t srtpPolicy;

        std::memset(&srtpPolicy, 0, sizeof(srtpPolicy));
        srtpPolicy.ssrc.type = ssrc_any_outbound;
        srtpPolicy.key = bufMasterCombined.data();
        srtpPolicy.allow_repeat_tx = true;

        srtp_crypto_policy_set_from_profile_for_rtp(&srtpPolicy.rtp, srtpProfileId);
        srtp_crypto_policy_set_from_profile_for_rtcp(&srtpPolicy.rtcp, srtpProfileId);

        srtp_t srtp = nullptr;
        ASSERT_EQ(srtp_create(&srtp, &srtpPolicy), srtp_err_status_ok);

        // Randomly generate RTCP packets, encrypt them using libSRTP and our crypto.
        // Verify that the result of encryption is the same for both.
        uint32_t ssrc = 0x12345678;
        uint32_t sequence = 1;

        const auto track = std::make_shared<srtc::Track>(
            0, srtc::MediaType::Video, "0", ssrc, 96, 0, 0, srtc::Codec::H264, nullptr, nullptr, 90000, false, false);

        for (auto repeatIndex = 0; repeatIndex < 5000; repeatIndex += 1) {
            // Packet 1
            const auto payloadSize1 = 5 + lrand48() % 200;
            srtc::ByteBuffer payload1(payloadSize1);
            RAND_bytes(payload1.data(), static_cast<int>(payload1.capacity()));
            payload1.resize(payloadSize1);

            const auto packet1 = std::make_shared<srtc::RtcpPacket>(track, 0x01, 0x22, std::move(payload1));

            // Packet 2
            const auto payloadSize2 = 5 + lrand48() % 200;
            srtc::ByteBuffer payload2(payloadSize2);
            RAND_bytes(payload2.data(), static_cast<int>(payload2.capacity()));
            payload2.resize(payloadSize2);

            const auto packet2 = std::make_shared<srtc::RtcpPacket>(track, 0x03, 0x44, std::move(payload2));

            // This is our unencrypted data
            const auto source1 = packet1->generate();
            const auto source2 = packet2->generate();

            // Combined into one
            srtc::ByteBuffer source;
            source.append(source1);
            source.append(source2);

            // Encrypt using libSRTP
            srtc::ByteBuffer protectedLibSrtp(source.size() + SRTP_MAX_TRAILER_LEN);
            size_t protectedSize = protectedLibSrtp.capacity();
            ASSERT_EQ(srtp_protect_rtcp(srtp, source.data(), source.size(), protectedLibSrtp.data(), &protectedSize, 0),
                      srtp_err_status_ok);
            protectedLibSrtp.resize(protectedSize);

            // Encrypt using our own crypto
            srtc::ByteBuffer protectedSrtcCrypto;
            ASSERT_TRUE(crypto->protectSendRtcp(source, sequence, protectedSrtcCrypto));

            // Validate
            ASSERT_EQ(protectedLibSrtp.size(), protectedSrtcCrypto.size());

            for (size_t i = 0; i < protectedSize; i += 1) {
                ASSERT_EQ(protectedLibSrtp.data()[i], protectedSrtcCrypto.data()[i])
                    << " diff at offset " << i << " of " << protectedLibSrtp.size() << " " << srtpProfileName
                    << std::endl;
            }

            // Advance
            sequence += 1;
        }
    }
}

// RTCP receive

TEST(SrtpCrypto, RtcpReceive)
{
    std::cout << "SrtpCrypto RtcpReceive" << std::endl;

    initLibSRTP();
    srtc::initOpenSSL();

    static const uint16_t kOpenSslProfileList[] = {
        SRTP_AEAD_AES_256_GCM, SRTP_AEAD_AES_128_GCM, SRTP_AES128_CM_SHA1_80, SRTP_AES128_CM_SHA1_32
    };

    for (const auto openSSlProfile : kOpenSslProfileList) {
        const char* srtpProfileName;
        srtp_profile_t srtpProfileId;
        size_t srtpKeySize = { 0 }, srtpSaltSize = { 0 };

        switch (openSSlProfile) {
        case SRTP_AEAD_AES_256_GCM:
            srtpProfileName = "SRTP_AEAD_AES_256_GCM";
            srtpProfileId = srtp_profile_aead_aes_256_gcm;
            srtpKeySize = SRTP_AES_256_KEY_LEN;
            srtpSaltSize = SRTP_AEAD_SALT_LEN;
            break;
        case SRTP_AEAD_AES_128_GCM:
            srtpProfileName = "SRTP_AEAD_AES_128_GCM";
            srtpProfileId = srtp_profile_aead_aes_128_gcm;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_AEAD_SALT_LEN;
            break;
        case SRTP_AES128_CM_SHA1_80:
            srtpProfileName = "SRTP_AES128_CM_SHA1_80";
            srtpProfileId = srtp_profile_aes128_cm_sha1_80;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_SALT_LEN;
            break;
        case SRTP_AES128_CM_SHA1_32:
            srtpProfileName = "SRTP_AES128_CM_SHA1_32";
            srtpProfileId = srtp_profile_aes128_cm_sha1_32;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_SALT_LEN;
            break;
        default:
            ASSERT_TRUE(false);
            break;
        }

        std::cout << "Testing " << srtpProfileName << std::endl;

        // Generate random keys and salts
        uint8_t bufSendMasterKey[32], bufSendMasterSalt[32];
        RAND_bytes(bufSendMasterKey, sizeof(bufSendMasterKey));
        RAND_bytes(bufSendMasterSalt, sizeof(bufSendMasterSalt));

        uint8_t bufReceiveMasterKey[32], bufReceiveMasterSalt[32];
        RAND_bytes(bufReceiveMasterKey, sizeof(bufReceiveMasterKey));
        RAND_bytes(bufReceiveMasterSalt, sizeof(bufReceiveMasterSalt));

        // Convert to our objects
        srtc::CryptoBytes sendMasterKey, sendMasterSalt;
        sendMasterKey.assign(bufSendMasterKey, srtpKeySize);
        sendMasterSalt.assign(bufSendMasterSalt, srtpSaltSize);

        srtc::CryptoBytes receiveMasterKey, receiveMasterSalt;
        receiveMasterKey.assign(bufReceiveMasterKey, srtpKeySize);
        receiveMasterSalt.assign(bufReceiveMasterSalt, srtpSaltSize);

        // Create our own crypto
        const auto [crypto, error] = srtc::SrtpCrypto::create(
            openSSlProfile, sendMasterKey, sendMasterSalt, receiveMasterKey, receiveMasterSalt);
        ASSERT_TRUE(error.isOk());
        ASSERT_TRUE(crypto);

        // Create libSRTP crypto
        srtc::ByteBuffer bufMasterCombined;
        bufMasterCombined.append(bufReceiveMasterKey, srtpKeySize);
        bufMasterCombined.append(bufReceiveMasterSalt, srtpSaltSize);

        srtp_policy_t srtpPolicy;

        std::memset(&srtpPolicy, 0, sizeof(srtpPolicy));
        srtpPolicy.ssrc.type = ssrc_any_outbound;
        srtpPolicy.key = bufMasterCombined.data();
        srtpPolicy.allow_repeat_tx = true;

        srtp_crypto_policy_set_from_profile_for_rtp(&srtpPolicy.rtp, srtpProfileId);
        srtp_crypto_policy_set_from_profile_for_rtcp(&srtpPolicy.rtcp, srtpProfileId);

        srtp_t srtp = nullptr;
        ASSERT_EQ(srtp_create(&srtp, &srtpPolicy), srtp_err_status_ok);

        // Randomly generate RTCP packets, encrypt them using libSRTP and decrypt using our crypto
        // Verify that the result of decryption matches the original
        uint32_t ssrc = 0x12345678;

        for (auto repeatIndex = 0; repeatIndex < 5000; repeatIndex += 1) {
            srtc::ByteBuffer sourcePacket;
            srtc::ByteWriter sourceWriter(sourcePacket);

            const auto sourcePacketDataSizeDiv4 = 1 + lrand48() % 16;

            // Verison = 2, Padding, and RC
            sourceWriter.writeU8(0x80);
            // Payload type
            sourceWriter.writeU8(201);
            // Length in units of 32-bit words minus one
            sourceWriter.writeU16(sourcePacketDataSizeDiv4 - 1);
            // SSRC
            sourceWriter.writeU32(ssrc);

            // Random data
            const auto sourcePacketDataSize = 4 * sourcePacketDataSizeDiv4;
            ASSERT_LE(sourcePacketDataSize, 64);
            uint8_t randomBytes[64];
            RAND_bytes(randomBytes, sizeof(randomBytes));
            sourceWriter.write(randomBytes, sourcePacketDataSize);

            // Encrypt
            srtc::ByteBuffer encryptedPacket;
            encryptedPacket.reserve(sourcePacket.size() + SRTP_MAX_TRAILER_LEN);
            size_t encryptedLen = encryptedPacket.capacity();

            ASSERT_EQ(srtp_protect_rtcp(
                          srtp, sourcePacket.data(), sourcePacket.size(), encryptedPacket.data(), &encryptedLen, 0),
                      srtp_err_status_ok);
            encryptedPacket.resize(encryptedLen);

            // Decrypt using our crypto
            srtc::ByteBuffer decryptedPacket;
            const auto decryptedResult = crypto->unprotectReceiveRtcp(encryptedPacket, decryptedPacket);
            ASSERT_TRUE(decryptedResult);

            // The packet should be equal to the source
            ASSERT_EQ(decryptedPacket.size(), sourcePacket.size());
            ASSERT_EQ(std::memcmp(decryptedPacket.data(), sourcePacket.data(), decryptedPacket.size()), 0);
        }

        // Cleanup
        srtp_dealloc(srtp);
    }
}

// RTCP receive - multiple RTCP packets in one UDP packet

TEST(SrtpCrypto, RtcpReceiveMulti)
{
    std::cout << "SrtpCrypto RtcpReceiveMulti" << std::endl;

    initLibSRTP();
    srtc::initOpenSSL();

    static const uint16_t kOpenSslProfileList[] = {
        SRTP_AEAD_AES_256_GCM, SRTP_AEAD_AES_128_GCM, SRTP_AES128_CM_SHA1_80, SRTP_AES128_CM_SHA1_32
    };

    for (const auto openSSlProfile : kOpenSslProfileList) {
        const char* srtpProfileName;
        srtp_profile_t srtpProfileId;
        size_t srtpKeySize = { 0 }, srtpSaltSize = { 0 };

        switch (openSSlProfile) {
        case SRTP_AEAD_AES_256_GCM:
            srtpProfileName = "SRTP_AEAD_AES_256_GCM";
            srtpProfileId = srtp_profile_aead_aes_256_gcm;
            srtpKeySize = SRTP_AES_256_KEY_LEN;
            srtpSaltSize = SRTP_AEAD_SALT_LEN;
            break;
        case SRTP_AEAD_AES_128_GCM:
            srtpProfileName = "SRTP_AEAD_AES_128_GCM";
            srtpProfileId = srtp_profile_aead_aes_128_gcm;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_AEAD_SALT_LEN;
            break;
        case SRTP_AES128_CM_SHA1_80:
            srtpProfileName = "SRTP_AES128_CM_SHA1_80";
            srtpProfileId = srtp_profile_aes128_cm_sha1_80;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_SALT_LEN;
            break;
        case SRTP_AES128_CM_SHA1_32:
            srtpProfileName = "SRTP_AES128_CM_SHA1_32";
            srtpProfileId = srtp_profile_aes128_cm_sha1_32;
            srtpKeySize = SRTP_AES_128_KEY_LEN;
            srtpSaltSize = SRTP_SALT_LEN;
            break;
        default:
            ASSERT_TRUE(false);
            break;
        }

        std::cout << "Testing " << srtpProfileName << std::endl;

        // Generate random keys and salts
        uint8_t bufSendMasterKey[32], bufSendMasterSalt[32];
        RAND_bytes(bufSendMasterKey, sizeof(bufSendMasterKey));
        RAND_bytes(bufSendMasterSalt, sizeof(bufSendMasterSalt));

        uint8_t bufReceiveMasterKey[32], bufReceiveMasterSalt[32];
        RAND_bytes(bufReceiveMasterKey, sizeof(bufReceiveMasterKey));
        RAND_bytes(bufReceiveMasterSalt, sizeof(bufReceiveMasterSalt));

        // Convert to our objects
        srtc::CryptoBytes sendMasterKey, sendMasterSalt;
        sendMasterKey.assign(bufSendMasterKey, srtpKeySize);
        sendMasterSalt.assign(bufSendMasterSalt, srtpSaltSize);

        srtc::CryptoBytes receiveMasterKey, receiveMasterSalt;
        receiveMasterKey.assign(bufReceiveMasterKey, srtpKeySize);
        receiveMasterSalt.assign(bufReceiveMasterSalt, srtpSaltSize);

        // Create our own crypto
        const auto [crypto, error] = srtc::SrtpCrypto::create(
            openSSlProfile, sendMasterKey, sendMasterSalt, receiveMasterKey, receiveMasterSalt);
        ASSERT_TRUE(error.isOk());
        ASSERT_TRUE(crypto);

        // Create libSRTP crypto
        srtc::ByteBuffer bufMasterCombined;
        bufMasterCombined.append(bufReceiveMasterKey, srtpKeySize);
        bufMasterCombined.append(bufReceiveMasterSalt, srtpSaltSize);

        srtp_policy_t srtpPolicy;

        std::memset(&srtpPolicy, 0, sizeof(srtpPolicy));
        srtpPolicy.ssrc.type = ssrc_any_outbound;
        srtpPolicy.key = bufMasterCombined.data();
        srtpPolicy.allow_repeat_tx = true;

        srtp_crypto_policy_set_from_profile_for_rtp(&srtpPolicy.rtp, srtpProfileId);
        srtp_crypto_policy_set_from_profile_for_rtcp(&srtpPolicy.rtcp, srtpProfileId);

        srtp_t srtp = nullptr;
        ASSERT_EQ(srtp_create(&srtp, &srtpPolicy), srtp_err_status_ok);

        // Randomly generate RTCP packets, encrypt them using libSRTP and decrypt using our crypto
        // Verify that the result of decryption matches the original
        uint32_t ssrc = 0x12345678;

        for (auto repeatIndex = 0; repeatIndex < 5000; repeatIndex += 1) {
            // Packet 1
            srtc::ByteBuffer sourcePacket1;
            srtc::ByteWriter sourceWriter1(sourcePacket1);

            const auto sourcePacket1DataSizeDiv4 = 1 + lrand48() % 16;

            // Verison = 2, Padding, and RC
            sourceWriter1.writeU8(0x80);
            // Payload type
            sourceWriter1.writeU8(201);
            // Length in units of 32-bit words minus one
            sourceWriter1.writeU16(sourcePacket1DataSizeDiv4 - 1);
            // SSRC
            sourceWriter1.writeU32(ssrc);

            // Random data
            const auto sourcePacket1DataSize = 4 * sourcePacket1DataSizeDiv4;
            ASSERT_LE(sourcePacket1DataSize, 64);
            uint8_t randomBytes1[64];
            RAND_bytes(randomBytes1, sizeof(randomBytes1));
            sourceWriter1.write(randomBytes1, sourcePacket1DataSize);

            // Packet 2
            srtc::ByteBuffer sourcePacket2;
            srtc::ByteWriter sourceWriter2(sourcePacket2);

            const auto sourcePacket2DataSizeDiv4 = 1 + lrand48() % 16;

            // Verison = 2, Padding, and RC
            sourceWriter2.writeU8(0x80);
            // Payload type
            sourceWriter2.writeU8(201);
            // Length in units of 32-bit words minus one
            sourceWriter2.writeU16(sourcePacket2DataSizeDiv4 - 1);
            // SSRC
            sourceWriter2.writeU32(ssrc);

            // Random data
            const auto sourcePacket2DataSize = 4 * sourcePacket2DataSizeDiv4;
            ASSERT_LE(sourcePacket2DataSize, 64);
            uint8_t randomBytes2[64];
            RAND_bytes(randomBytes2, sizeof(randomBytes2));
            sourceWriter1.write(randomBytes2, sourcePacket2DataSize);

            // Combine into one
            srtc::ByteBuffer sourcePacket;
            sourcePacket.append(sourcePacket1);
            sourcePacket.append(sourcePacket2);

            // Encrypt
            srtc::ByteBuffer encryptedPacket;
            encryptedPacket.reserve(sourcePacket.size() + SRTP_MAX_TRAILER_LEN);
            size_t encryptedLen = encryptedPacket.capacity();

            ASSERT_EQ(srtp_protect_rtcp(
                          srtp, sourcePacket.data(), sourcePacket.size(), encryptedPacket.data(), &encryptedLen, 0),
                      srtp_err_status_ok);
            encryptedPacket.resize(encryptedLen);

            // Decrypt using our crypto
            srtc::ByteBuffer decryptedPacket;
            const auto decryptedResult = crypto->unprotectReceiveRtcp(encryptedPacket, decryptedPacket);
            ASSERT_TRUE(decryptedResult);

            // The packet should be equal to the source
            ASSERT_EQ(decryptedPacket.size(), sourcePacket.size());
            ASSERT_EQ(std::memcmp(decryptedPacket.data(), sourcePacket.data(), decryptedPacket.size()), 0);
        }

        // Cleanup
        srtp_dealloc(srtp);
    }
}

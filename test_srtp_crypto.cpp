#include <gtest/gtest.h>

#include "srtc/srtp_util.h"
#include "srtc/byte_buffer.h"
#include "srtc/srtp_crypto.h"
#include "srtc/srtp_openssl.h"

#include <mutex>
#include <cstring>

#include <srtp.h>
#include <openssl/srtp.h>
#include <openssl/rand.h>

// Init Cisco's libSRTP

namespace {

std::once_flag gSrtpInitFlag;

void initLibSRTP()
{
    std::call_once(gSrtpInitFlag, [] {
                       srtp_init();
                   }
    );
}

}

// RTCP receive

TEST(SrtpCrypto, RtcpReceive)
{
    std::cout << "SrtpCrypto RtcpReceive" << std::endl;

    initLibSRTP();
    srtc::initOpenSSL();

    static const uint16_t kOpenSslProfileList[] = {
            SRTP_AEAD_AES_256_GCM,
            SRTP_AEAD_AES_128_GCM
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

        // Generate the key and the salt
        uint8_t bufMasterKey[32], bufMasterSalt[32];
        RAND_bytes(bufMasterKey, sizeof(bufMasterKey));
        RAND_bytes(bufMasterSalt, sizeof(bufMasterSalt));

        // Create our own crypto
        srtc::CryptoBytes receiveMasterKey, receiveMasterSalt;
        receiveMasterKey.assign(bufMasterKey, srtpKeySize);
        receiveMasterSalt.assign(bufMasterSalt, srtpSaltSize);

        const auto [crypto, error] = srtc::SrtpCrypto::create(openSSlProfile, receiveMasterKey, receiveMasterSalt);
        ASSERT_TRUE(error.isOk());
        ASSERT_TRUE(crypto);

        // Create libSRTP crypto
        srtc::ByteBuffer bufMasterCombined;
        bufMasterCombined.append(bufMasterKey, srtpKeySize);
        bufMasterCombined.append(bufMasterSalt, srtpSaltSize);

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

        for (auto repeatIndex = 0; repeatIndex < 1000; repeatIndex += 1) {
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

            ASSERT_EQ(srtp_protect_rtcp(srtp, sourcePacket.data(), sourcePacket.size(),
                                        encryptedPacket.data(), &encryptedLen, 0),
                      srtp_err_status_ok);
            encryptedPacket.resize(encryptedLen);

            // Decrypt using our crypto
            srtc::ByteBuffer decryptedPacket;
            const auto decryptedSize = crypto->unprotectReceiveRtcp(encryptedPacket, decryptedPacket);
            ASSERT_GT(decryptedSize, 0);
            decryptedPacket.resize(decryptedSize);

            // The packet should be equal to the source
            ASSERT_EQ(decryptedPacket.size(), sourcePacket.size());
            ASSERT_EQ(std::memcmp(decryptedPacket.data(), sourcePacket.data(), decryptedSize), 0);
        }

        // Cleanup
        srtp_dealloc(srtp);
    }
}

#include <gtest/gtest.h>

#include "srtc/byte_buffer.h"
#include "srtc/srtp_util.h"

#include <cstring>
#include <mutex>

#include <openssl/srtp.h>
#include <srtp.h>

// Init Cisco's libSRTP

namespace
{

std::once_flag gSrtpInitFlag;

void initLibSRTP()
{
    std::call_once(gSrtpInitFlag, [] { srtp_init(); });
}

void setFromHex(srtc::CryptoBytes& bytes, const char* hex)
{
    srtc::CryptoWriter w(bytes);

    uint8_t value = 0;
    uint8_t count = 0;
    size_t byteCount = 0;

    while (*hex) {
        const auto ch = *hex++;

        if (ch >= '0' && ch <= '9') {
            value = (value << 4) | (ch - '0');
            count += 1;
        } else if (ch >= 'a' && ch <= 'f') {
            value = (value << 4) | (ch - 'a' + 10);
            count += 1;
        } else if (ch >= 'A' && ch <= 'F') {
            value = (value << 4) | (ch - 'A' + 10);
            count += 1;
        }

        if (count == 2) {
            w.writeU8(value);
            value = 0;
            count = 0;
            byteCount += 1;
        }
    }
}

std::string toHex(const uint8_t* data, size_t len)
{
    static constexpr auto alphabet = "0123456789ABCDEF";

    std::string s;
    s.reserve(len * 2);

    for (size_t i = 0; i < len; i += 1) {
        const auto value = data[i];
        s += alphabet[(value >> 4) & 0x0F];
        s += alphabet[value & 0x0F];
    }

    return s;
}

std::string toHex(const srtc::CryptoBytes& bytes)
{
    return toHex(bytes.data(), bytes.size());
}

} // namespace

// Key derivation

TEST(KeyDerivation, TestRfc)
{
    std::cout << "KeyDerivation TestRfc" << std::endl;

    initLibSRTP();

    srtc::CryptoBytes masterKey;
    setFromHex(masterKey, "E1F97A0D3E018BE0D64FA32C06DE4139");

    srtc::CryptoBytes masterSalt;
    setFromHex(masterSalt, "0EC675AD498AFEEBB6960B3AABE6");

    srtc::CryptoBytes outputLabel0;
    ASSERT_TRUE(srtc::KeyDerivation::generate(masterKey, masterSalt, 0, outputLabel0, 16));
    ASSERT_TRUE(outputLabel0.size() == 16);

    const auto outputLabel0Str = toHex(outputLabel0);
    std::cout << outputLabel0Str << std::endl;
    ASSERT_EQ(outputLabel0Str, "C61E7A93744F39EE10734AFE3FF7A087");

    srtc::CryptoBytes outputLabel1;
    ASSERT_TRUE(srtc::KeyDerivation::generate(masterKey, masterSalt, 1, outputLabel1, 32));
    ASSERT_TRUE(outputLabel1.size() == 32);

    const auto outputLabel1Str = toHex(outputLabel1);
    std::cout << outputLabel1Str << std::endl;
    ASSERT_EQ(outputLabel1Str, "CEBE321F6FF7716B6FD4AB49AF256A156D38BAA48F0A0ACF3C34E2359E6CDBCE");

    srtc::CryptoBytes outputLabel2;
    ASSERT_TRUE(srtc::KeyDerivation::generate(masterKey, masterSalt, 2, outputLabel2, 14));
    ASSERT_TRUE(outputLabel2.size() == 14);

    const auto outputLabel2Str = toHex(outputLabel2);
    std::cout << outputLabel2Str << std::endl;
    ASSERT_EQ(outputLabel2Str, "30CBBC08863D8C85D49DB34A9AE1");
}

TEST(KeyDerivation, TestSimpleInbound)
{

    std::cout << "KeyDerivation TestSimpleInbound" << std::endl;

    initLibSRTP();

    const uint8_t kMasterKey[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    const uint8_t kMasterSalt[] = { 31, 32, 33, 34, 35, 36, 41, 42, 43, 44, 45, 46 };

    const auto profileSSL = SRTP_AEAD_AES_128_GCM;
    const auto profileSRTP = srtp_profile_aead_aes_128_gcm;

    srtc::CryptoBytes masterKey;
    masterKey.assign(kMasterKey, sizeof(kMasterKey));

    srtc::CryptoBytes masterSalt;
    masterSalt.assign(kMasterSalt, sizeof(kMasterSalt));

    srtp_policy_t policy = {};

    srtc::ByteBuffer masterCombined;
    masterCombined.append(masterKey.data(), 16);
    masterCombined.append(masterSalt.data(), 12);

    policy.ssrc.type = ssrc_any_inbound;
    policy.key = masterCombined.data();
    policy.allow_repeat_tx = false;

    srtp_crypto_policy_set_from_profile_for_rtp(&policy.rtp, profileSRTP);
    srtp_crypto_policy_set_from_profile_for_rtcp(&policy.rtcp, profileSRTP);

    // This performs key derivation
    srtp_t srtp = nullptr;
    srtp_create(&srtp, &policy);

    // Now try ours
    srtc::CryptoBytes outputLabel5;
    ASSERT_TRUE(srtc::KeyDerivation::generate(masterKey, masterSalt, srtc::KeyDerivation::kLabelRtcpSalt, outputLabel5, 12));

    const auto outputLabel5Str = toHex(outputLabel5);
    std::cout << outputLabel5Str << std::endl;
    ASSERT_EQ(outputLabel5Str, "531B07167D1305116AFFD2B4");

    // Cleanup
    srtp_dealloc(srtp);
}

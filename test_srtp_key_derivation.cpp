#include <gtest/gtest.h>

#include "srtc/srtp_util.h"
#include "srtc/byte_buffer.h"

#include <mutex>
#include <cstring>

#include <srtp.h>
#include <openssl/srtp.h>
#include <openssl/evp.h>

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

size_t setFromHex(srtc::CryptoBytes& bytes, const char* hex)
{
    srtc::CryptoBytesWriter w(bytes);

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

    return byteCount;
}

std::string toHex(const uint8_t* data, size_t len) {
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

std::string toHex(const srtc::CryptoBytes& bytes, size_t len)
{
    return toHex(bytes.data(), len);
}

size_t deriveKey(const srtc::CryptoBytes& masterKey,
                 size_t masterKeyLen,
                 const srtc::CryptoBytes& masterSalt,
                 size_t masterSaltLen,
                 uint8_t label,
                 srtc::CryptoBytes& output)
{
    assert(masterKeyLen == 16 || masterKeyLen == 32);
    assert(masterSaltLen == 12 || masterSaltLen == 14);

    const auto masterKeyStr = toHex(masterKey, masterKeyLen);
    const auto masterSaltStr = toHex(masterSalt, masterSaltLen);

    // https://datatracker.ietf.org/doc/html/rfc3711#appendix-B.3

    uint8_t input[16] = {};
    std::memcpy(input, masterSalt.data(), masterSaltLen);
    input[7] ^= label;

    const auto inputStr = toHex(input, 16);

    const auto ctx = EVP_CIPHER_CTX_new();

    uint8_t outbuf[16];
    int outlen = 0;
    int outtotal = 0;

    const auto cipher = masterKeyLen == 16 ?
                        EVP_aes_128_ctr() : EVP_aes_256_ctr();
    if (!EVP_EncryptInit_ex(ctx, cipher, nullptr, masterKey.data(), nullptr)) {
        goto exit;
    }

    if (!EVP_EncryptUpdate(ctx, outbuf, &outlen, input, 16)) {
        goto exit;
    }
    outtotal = outlen;

    if (!EVP_EncryptFinal_ex(ctx, outbuf + outlen, &outlen)) {
        outtotal = 0;
        goto exit;
    }
    outtotal += outlen;

    output.assign(outbuf, outtotal);

exit:
    const auto outputStr = toHex(outbuf, outtotal);

    EVP_CIPHER_CTX_free(ctx);
    return outtotal;
}

}

// Key derivation

TEST(KeyDerivation, TestRfc) {

    srtc::CryptoBytes masterKey;
    const auto masterKeyLen = setFromHex(masterKey, "E1F97A0D3E018BE0D64FA32C06DE4139");

    srtc::CryptoBytes masterSalt;
    const auto masterSaltLen = setFromHex(masterSalt, "0EC675AD498AFEEBB6960B3AABE6");

    srtc::CryptoBytes outputLabel0;
    const auto outputLabel0Len = deriveKey(masterKey, masterKeyLen, masterSalt, masterSaltLen, 0, outputLabel0);
    ASSERT_TRUE(outputLabel0Len > 0);

    const auto outputLabel0Str = toHex(outputLabel0, outputLabel0Len);

    std::cout << outputLabel0Str << std::endl;

}


TEST(KeyDerivation, TestSimpleInbound) {

    std::cout << "KeyDerivation TestSimpleInbound" << std::endl;

    initLibSRTP();

    const uint8_t kMasterKey[] = {
            1, 2, 3, 4, 5, 6, 7, 8,
            9, 10, 11, 12, 13, 14, 15, 16
    };
    const uint8_t kMasterSalt[] = {
            31, 32, 33, 34, 35, 36,
            41, 42, 43, 44, 45, 46
    };

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

    // Cleanup
    srtp_dealloc(srtp);
}

#include <gtest/gtest.h>

#include "srtc/srtp_util.h"
#include "srtc/byte_buffer.h"

#include <mutex>

#include "srtp.h"
#include "openssl/srtp.h"

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

// Key derivation

TEST(KeyDerivation, TestSimpleInbound) {

    std::cout << "KeyDerivation TestSimpleInbound" << std::endl;

    initLibSRTP();

    const auto profileSSL = SRTP_AEAD_AES_128_GCM;
    const auto profileSRTP = srtp_profile_aead_aes_128_gcm;

    srtc::CryptoBytes masterKey;
    srtc::CryptoBytesWriter masterKeyW(masterKey);
    masterKeyW.writeU32(0x10111213u);
    masterKeyW.writeU32(0x20212223u);
    masterKeyW.writeU16(0x3031u);
    masterKeyW.writeU16(0x4041u);
    masterKeyW.writeU16(0x5051u);
    masterKeyW.writeU16(0x6061u);

    srtc::CryptoBytes masterSalt;
    srtc::CryptoBytesWriter masterSaltW(masterSalt);
    masterSaltW.writeU32(0x70717273u);
    masterSaltW.writeU32(0x80818283u);
    masterSaltW.writeU32(0x90919293u);

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

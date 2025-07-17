#pragma once

#include <memory>

#include "srtc/error.h"
#include "srtc/srtp_util.h"

struct evp_cipher_ctx_st;
struct evp_cipher_st;

namespace srtc
{

class ByteBuffer;
class HmacSha1;

class SrtpCrypto
{
public:
    [[nodiscard]] static std::pair<std::shared_ptr<SrtpCrypto>, Error> create(uint64_t profileId,
                                                                              const CryptoBytes& sendMasterKey,
                                                                              const CryptoBytes& sendMasterSalt,
                                                                              const CryptoBytes& receiveMasterKey,
                                                                              const CryptoBytes& receiveMasterSalt);

    ~SrtpCrypto();

    [[nodiscard]] size_t getMediaProtectionOverhead() const;

    [[nodiscard]] bool protectSendMedia(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& encrypted);
	[[nodiscard]] bool unprotectReceiveMedia(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& plain);

    [[nodiscard]] bool protectSendControl(const ByteBuffer& packet, uint32_t seq, ByteBuffer& encrypted);
    [[nodiscard]] bool unprotectReceiveControl(const ByteBuffer& packet, ByteBuffer& plain);

	static bool secureEquals(const void* a, const void* b, size_t size);

    // Implementation
    struct CryptoVectors {
        CryptoBytes key;
        CryptoBytes auth;
        CryptoBytes salt;
    };

    SrtpCrypto(uint64_t profileId,
			   // Send RTP
			   const CryptoVectors& sendRtp,
			   // Receive RTP
			   const CryptoVectors& receiveRtp,
               // Send RTCP
               const CryptoVectors& sendRtcp,
               // Receive RTCP
               const CryptoVectors& receiveRtcp);

private:
    [[nodiscard]] bool protectSendMediaGCM(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& encrypted);
    [[nodiscard]] bool protectSendMediaCM(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& encrypted);

	[[nodiscard]] bool unprotectReceiveMediaGCM(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& plain);
	[[nodiscard]] bool unprotectReceiveMediaCM(const ByteBuffer& packet, uint32_t rolloverCount, ByteBuffer& plain);

    [[nodiscard]] bool protectSendControlGCM(const ByteBuffer& packet, uint32_t seq, ByteBuffer& encrypted);
    [[nodiscard]] bool protectSendControlCM(const ByteBuffer& packet, uint32_t seq, ByteBuffer& encrypted);

    [[nodiscard]] bool unprotectReceiveControlGCM(const ByteBuffer& packet, ByteBuffer& plain);
    [[nodiscard]] bool unprotectReceiveControlCM(const ByteBuffer& packet, ByteBuffer& plain);

    [[nodiscard]] const struct evp_cipher_st* createCipher() const;

    const uint64_t mProfileId;
	const CryptoVectors mSendRtp;
	const CryptoVectors mReceiveRtp;
    const CryptoVectors mSendRtcp;
    const CryptoVectors mReceiveRtcp;

    struct evp_cipher_ctx_st* mSendCipherCtx;
    struct evp_cipher_ctx_st* mReceiveCipherCtx;

    std::shared_ptr<HmacSha1> mHmacSha1;
};

} // namespace srtc

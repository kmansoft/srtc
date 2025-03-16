#pragma once

#include <memory>

#include "srtc/error.h"
#include "srtc/srtp_util.h"

struct evp_cipher_ctx_st;

namespace srtc {

class ByteBuffer;

class SrtpCrypto {
public:
    [[nodiscard]] static std::pair<std::shared_ptr<SrtpCrypto>, Error> create(
            uint16_t profileId,
            const CryptoBytes& receiveMasterKey,
            const CryptoBytes& receiveMasterSalt
            );

    ~SrtpCrypto();

    size_t unprotectReceiveRtcp(const ByteBuffer& packet,
                                ByteBuffer& plain);

    // Implementation
    SrtpCrypto(uint16_t profileId,
               const CryptoBytes& receiveRtcpKey,
               const CryptoBytes& receiveRtcpAuth,
               const CryptoBytes& receiveRtcpSalt);

private:
    void computeReceiveRtcpIV(CryptoBytes& iv,
                              uint32_t ssrc,
                              uint32_t seq);
    [[nodiscard]] size_t unprotectReceiveRtcpGCM(const ByteBuffer& packet,
                                                 ByteBuffer& plain);
    [[nodiscard]] size_t unprotectReceiveRtcpCM(const ByteBuffer& packet,
                                                ByteBuffer& plain);

    const uint16_t mProfileId;
    const CryptoBytes mReceiveRtcpKey, mReceiveRtcpAuth, mReceiveRtcpSalt;

    struct evp_cipher_ctx_st* mReceiveCipherCtx;
};

}

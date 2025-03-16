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

    [[nodiscard]] size_t unprotectReceiveRtcp(const ByteBuffer& packet,
                                              ByteBuffer& plain);

    // Implementation
    SrtpCrypto(uint16_t profileId,
               const CryptoBytes& receiveRtcpKey,
               const CryptoBytes& receiveRtcpSalt);

private:
    [[nodiscard]] size_t unprotectReceiveRtcpAESGCM(const ByteBuffer& packet,
                                                    ByteBuffer& plain);

    const uint16_t mProfileId;
    const CryptoBytes mReceiveRtcpKey, mReceiveRtcpSalt;

    struct evp_cipher_ctx_st* mCipherDecryptCTX;

};

}

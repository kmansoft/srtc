#pragma once

#include <cstdint>

#include "stunmessage.h"

namespace srtc {

class IceAgent {
public:
    IceAgent();
    ~IceAgent();

    // https://datatracker.ietf.org/doc/html/rfc5389#section-6
    static constexpr auto kRfc5389Cookie = 0x2112A442;

    bool initRequest(StunMessage *msg,
                     uint8_t *buffer, size_t buffer_len, StunMethod m);
    bool initResponse(StunMessage *msg,
                      uint8_t *buffer, size_t buffer_len,
                      const StunMessage *request);
    bool finishMessage(StunMessage *msg,
                       const uint8_t *key, size_t key_len);

    bool forgetTransaction(StunTransactionId id);

private:
    const uint64_t mTie;
};

}

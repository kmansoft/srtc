#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <vector>
#include <cstring>

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
                       const std::string& username,
                       const std::string& password);

    bool forgetTransaction(StunTransactionId id);

private:
    struct SavedTransaction {
        SavedTransaction(std::chrono::steady_clock::time_point when,
                         const StunTransactionId& id)
                         : when(when), id() {
            std::memcpy(this->id, id, sizeof(StunTransactionId));
        }

        std::chrono::steady_clock::time_point when;
        StunTransactionId id;
    };

    const uint64_t mTie;
    std::vector<SavedTransaction> mTransactionList;
};

}

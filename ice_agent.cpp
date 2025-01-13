#include <cstdlib>
#include <memory>

#include "stunmessage.h"
#include "stun5389.h"
#include "stunhmac.h"

#include "srtc/ice_agent.h"

namespace {

const auto kSoftware = "srtc";

}

namespace srtc {

IceAgent::IceAgent()
    : mTie(((uint64_t)lrand48()) << 32 | lrand48())
{
}

IceAgent::~IceAgent() = default;

bool IceAgent::initRequest(StunMessage *msg,
                           uint8_t *buffer, size_t buffer_len, StunMethod m)
{
    std::memset(msg, 0, sizeof(*msg));
    msg->buffer = buffer;
    msg->buffer_len = buffer_len;

    StunTransactionId id;
    stun_make_transid(id);

    if (!stun_message_init (msg, STUN_REQUEST, m, id)) {
        return false;
    }

    uint32_t cookie = htonl (STUN_MAGIC_COOKIE);
    memcpy (msg->buffer + STUN_MESSAGE_TRANS_ID_POS, &cookie, sizeof (cookie));

    stun_message_append_software(msg, kSoftware);

    return true;
}

bool IceAgent::initResponse(StunMessage *msg,
                            uint8_t *buffer, size_t buffer_len,
                            const StunMessage *request)
{
    if (stun_message_get_class (request) != STUN_REQUEST) {
        return false;
    }

    std::memset(msg, 0, sizeof(*msg));
    msg->buffer = buffer;
    msg->buffer_len = buffer_len;

    StunTransactionId id;
    stun_message_id (request, id);

    if (!stun_message_init (msg, STUN_RESPONSE, stun_message_get_method (request), id)) {
        return false;
    }

    stun_message_append_software(msg, kSoftware);

    return true;
}

bool IceAgent::finishMessage(StunMessage *msg,
                             const uint8_t *key, size_t key_len)
{
    bool remember_transaction = (stun_message_get_class (msg) == STUN_REQUEST);

    auto ptr = stun_message_append (msg, STUN_ATTRIBUTE_MESSAGE_INTEGRITY, 20);
    if (!ptr) {
        return false;
    }

    stun_sha1 (msg->buffer, stun_message_length (msg),
               stun_message_length (msg) - 20, reinterpret_cast<uint8_t *>(ptr),
               key, key_len, false);

    ptr = stun_message_append (msg, STUN_ATTRIBUTE_FINGERPRINT, 4);
    if (!ptr) {
        return false;
    }

    auto fpr = stun_fingerprint (msg->buffer, stun_message_length (msg), false);
    std::memcpy (ptr, &fpr, sizeof (fpr));

    return true;
}

}

#include <cstdlib>
#include <memory>

#include "stunmessage.h"
#include "stun5389.h"
#include "stunhmac.h"

#include "srtc/ice_agent.h"
#include "srtc/logging.h"

#define LOG(level, ...) srtc::log(level, "IceAgent", __VA_ARGS__)

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

    stun_message_append64 (msg, STUN_ATTRIBUTE_ICE_CONTROLLING, mTie);

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
                             const srtc::optional<std::string>& username,
                             const std::string& password)
{
    if (username.has_value()) {
        stun_message_append_string(msg, STUN_ATTRIBUTE_USERNAME,
                                   username.value().c_str());
    }

    auto ptr = stun_message_append (msg, STUN_ATTRIBUTE_MESSAGE_INTEGRITY, 20);
    if (!ptr) {
        return false;
    }

    stun_sha1 (msg->buffer, stun_message_length (msg),
               stun_message_length (msg) - 20, reinterpret_cast<uint8_t *>(ptr),
               reinterpret_cast<const uint8_t*>(password.data()), password.size(), false);

    ptr = stun_message_append (msg, STUN_ATTRIBUTE_FINGERPRINT, 4);
    if (!ptr) {
        return false;
    }

    auto fpr = stun_fingerprint (msg->buffer, stun_message_length (msg), false);
    std::memcpy (ptr, &fpr, sizeof (fpr));

    if (stun_message_get_class (msg) == STUN_REQUEST) {
        StunTransactionId id;
        stun_message_id (msg, id);
        mTransactionList.emplace_back(std::chrono::steady_clock::now(), id);
    }

    return true;
}

bool IceAgent::forgetTransaction(StunTransactionId id)
{
    for (auto it = mTransactionList.begin(); it != mTransactionList.end(); ++it) {
        if (std::memcmp(it->id, id, sizeof(StunTransactionId)) == 0) {
            mTransactionList.erase(it);
            return true;
        }
    }

    return false;
}

void IceAgent::forgetExpiredTransactions(const std::chrono::milliseconds& expiration)
{
    const auto now = std::chrono::steady_clock::now();

    for (auto it = mTransactionList.begin(); it != mTransactionList.end(); ) {
        if (it->when < now - expiration) {
            it = mTransactionList.erase(it);
        } else {
            ++ it;
        }
    }
}

bool IceAgent::verifyRequestMessage(StunMessage* msg,
                                    const std::string& username,
                                    const std::string& password)
{
    // Fingerprint
    uint16_t attrFingerprintLen = { 0 };
    auto attrFingerprintPtr = stun_message_find(msg, STUN_ATTRIBUTE_FINGERPRINT, &attrFingerprintLen);

    if (!attrFingerprintPtr || attrFingerprintLen != 4) {
        LOG(SRTC_LOG_E, "Request verification failed: no fingerprint or invalid size");
        return false;
    }

    uint32_t fingerprintMessage;
    std::memcpy(&fingerprintMessage, attrFingerprintPtr, 4);

    const auto fingerprintCalculated = stun_fingerprint(msg->buffer,
                                                        stun_message_length(msg),
                                                        false);

    if (fingerprintMessage != fingerprintCalculated) {
        LOG(SRTC_LOG_E, "Request verification failed: fingerprint does not match");
        return false;
    }

    // Username
    uint16_t attrUserNameLen = { 0 };
    const auto attrUserNamePtr = stun_message_find(msg, STUN_ATTRIBUTE_USERNAME, &attrUserNameLen);

    if (!attrUserNamePtr || attrUserNameLen == 0) {
        LOG(SRTC_LOG_E, "Request verification failed: no username or invalid size");
        return false;
    }

    const std::string attrUserName { reinterpret_cast<const char*>(attrUserNamePtr), attrUserNameLen };
    if (attrUserName != username) {
        LOG(SRTC_LOG_E, "Request verification failed: username does not match");
        return false;
    }

    // Signature based on password
    uint16_t attrIntegrityLen = { 0 };
    const auto attrIntegrityPtr = stun_message_find(msg, STUN_ATTRIBUTE_MESSAGE_INTEGRITY, &attrIntegrityLen);

    if (!attrIntegrityPtr || attrIntegrityLen != 20) {
        LOG(SRTC_LOG_E, "Request verification failed: no signature or invalid size");
        return false;
    }

    const auto sha1Message = reinterpret_cast<const uint8_t*>(attrIntegrityPtr);
    uint8_t sha1Calculated[20];

    stun_sha1 (msg->buffer, sha1Message + 20 - msg->buffer,
               sha1Message - msg->buffer, sha1Calculated,
               reinterpret_cast<const uint8_t*>(password.data()), password.size(), false);

    if (std::memcmp(sha1Calculated, attrIntegrityPtr, 20) != 0) {
        LOG(SRTC_LOG_E, "Request verification failed: signature does not match");
        return false;
    }

    // Success
    return true;
}

bool IceAgent::verifyResponseMessage(StunMessage* msg,
                                     const std::string& password)
{
    // Fingerprint
    uint16_t attrFingerprintLen = { 0 };
    auto attrFingerprintPtr = stun_message_find(msg, STUN_ATTRIBUTE_FINGERPRINT, &attrFingerprintLen);

    if (!attrFingerprintPtr || attrFingerprintLen != 4) {
        LOG(SRTC_LOG_E, "Response verification failed: no fingerprint or invalid size");
        return false;
    }

    uint32_t fingerprintMessage;
    std::memcpy(&fingerprintMessage, attrFingerprintPtr, 4);

    const auto fingerprintCalculated = stun_fingerprint(msg->buffer,
                                                        stun_message_length(msg),
                                                        false);

    if (fingerprintMessage != fingerprintCalculated) {
        LOG(SRTC_LOG_E, "Response verification failed: fingerprint does not match");
        return false;
    }

    // Signature based on password
    uint16_t attrIntegrityLen = { 0 };
    const auto attrIntegrityPtr = stun_message_find(msg, STUN_ATTRIBUTE_MESSAGE_INTEGRITY, &attrIntegrityLen);

    if (!attrIntegrityPtr || attrIntegrityLen != 20) {
        LOG(SRTC_LOG_E, "Response verification failed: no signature or invalid size");
        return false;
    }

    const auto sha1Message = reinterpret_cast<const uint8_t*>(attrIntegrityPtr);
    uint8_t sha1Calculated[20];

    stun_sha1 (msg->buffer, sha1Message + 20 - msg->buffer,
               sha1Message - msg->buffer, sha1Calculated,
               reinterpret_cast<const uint8_t*>(password.data()), password.size(), false);

    if (std::memcmp(sha1Calculated, attrIntegrityPtr, 20) != 0) {
        LOG(SRTC_LOG_E, "Response verification failed: signature does not match");
        return false;
    }

    // Success
    return true;
}

}

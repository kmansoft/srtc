#include <cstdlib>
#include <memory>

#include "stun5389.h"
#include "stunhmac.h"
#include "stunmessage.h"

#include "srtc/ice_agent.h"
#include "srtc/logging.h"

#define LOG(level, ...) srtc::log(level, "IceAgent", __VA_ARGS__)

namespace
{

const auto kSoftware = "srtc";

}

namespace srtc
{

IceAgent::IceAgent()
	: mRandom(0, std::numeric_limits<int32_t>::max())
	, mTie(((uint64_t)mRandom.next()) << 32 | mRandom.next())
{
}

IceAgent::~IceAgent() = default;

bool IceAgent::initRequest(stun::StunMessage* msg, uint8_t* buffer, size_t buffer_len, stun::StunMethod m)
{
	std::memset(msg, 0, sizeof(*msg));
	msg->buffer = buffer;
	msg->buffer_len = buffer_len;

	stun::StunTransactionId id;
	stun::stun_make_transid(id);

	if (!stun::stun_message_init(msg, stun::STUN_REQUEST, m, id)) {
		return false;
	}

	uint32_t cookie = htonl(STUN_MAGIC_COOKIE);
	memcpy(msg->buffer + STUN_MESSAGE_TRANS_ID_POS, &cookie, sizeof(cookie));

	stun::stun_message_append_software(msg, kSoftware);

	stun::stun_message_append64(msg, stun::STUN_ATTRIBUTE_ICE_CONTROLLING, mTie);

	return true;
}

bool IceAgent::initResponse(stun::StunMessage* msg,
							uint8_t* buffer,
							size_t buffer_len,
							const stun::StunMessage* request)
{
	if (stun::stun_message_get_class(request) != stun::STUN_REQUEST) {
		return false;
	}

	std::memset(msg, 0, sizeof(*msg));
	msg->buffer = buffer;
	msg->buffer_len = buffer_len;

	stun::StunTransactionId id;
	stun::stun_message_id(request, id);

	if (!stun_message_init(msg, stun::STUN_RESPONSE, stun::stun_message_get_method(request), id)) {
		return false;
	}

	stun::stun_message_append_software(msg, kSoftware);

	return true;
}

bool IceAgent::finishMessage(stun::StunMessage* msg,
							 const std::optional<std::string>& username,
							 const std::string& password)
{
	if (username.has_value()) {
		stun::stun_message_append_string(msg, stun::STUN_ATTRIBUTE_USERNAME, username.value().c_str());
	}

	auto ptr = stun::stun_message_append(msg, stun::STUN_ATTRIBUTE_MESSAGE_INTEGRITY, 20);
	if (!ptr) {
		return false;
	}

	stun::stun_sha1(msg->buffer,
					stun_message_length(msg),
					stun_message_length(msg) - 20,
					reinterpret_cast<uint8_t*>(ptr),
					reinterpret_cast<const uint8_t*>(password.data()),
					password.size(),
					false);

	ptr = stun::stun_message_append(msg, stun::STUN_ATTRIBUTE_FINGERPRINT, 4);
	if (!ptr) {
		return false;
	}

	auto fpr = stun::stun_fingerprint(msg->buffer, stun_message_length(msg), false);
	std::memcpy(ptr, &fpr, sizeof(fpr));

	if (stun::stun_message_get_class(msg) == stun::STUN_REQUEST) {
		stun::StunTransactionId id;
		stun_message_id(msg, id);
		mTransactionList.emplace_back(std::chrono::steady_clock::now(), id);
	}

	return true;
}

bool IceAgent::forgetTransaction(stun::StunTransactionId id, float& outRtt)
{
	for (auto it = mTransactionList.begin(); it != mTransactionList.end(); ++it) {
		if (std::memcmp(it->id, id, sizeof(stun::StunTransactionId)) == 0) {
			const auto micros =
				std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - it->when);
			outRtt = static_cast<float>(micros.count()) / 1000.0f;
			mTransactionList.erase(it);
			return true;
		}
	}

	return false;
}

void IceAgent::forgetExpiredTransactions(const std::chrono::milliseconds& expiration)
{
	const auto now = std::chrono::steady_clock::now();

	for (auto it = mTransactionList.begin(); it != mTransactionList.end();) {
		if (it->when < now - expiration) {
			it = mTransactionList.erase(it);
		} else {
			++it;
		}
	}
}

bool IceAgent::verifyRequestMessage(stun::StunMessage* msg, const std::string& username, const std::string& password)
{
	// Fingerprint
	uint16_t attrFingerprintLen = { 0 };
	auto attrFingerprintPtr = stun::stun_message_find(msg, stun::STUN_ATTRIBUTE_FINGERPRINT, &attrFingerprintLen);

	if (!attrFingerprintPtr || attrFingerprintLen != 4) {
		LOG(SRTC_LOG_E, "Request verification failed: no fingerprint or invalid size");
		return false;
	}

	uint32_t fingerprintMessage;
	std::memcpy(&fingerprintMessage, attrFingerprintPtr, 4);

	const auto fingerprintCalculated = stun::stun_fingerprint(msg->buffer, stun_message_length(msg), false);

	if (fingerprintMessage != fingerprintCalculated) {
		LOG(SRTC_LOG_E, "Request verification failed: fingerprint does not match");
		return false;
	}

	// Username
	uint16_t attrUserNameLen = { 0 };
	const auto attrUserNamePtr = stun_message_find(msg, stun::STUN_ATTRIBUTE_USERNAME, &attrUserNameLen);

	if (!attrUserNamePtr || attrUserNameLen == 0) {
		LOG(SRTC_LOG_E, "Request verification failed: no username or invalid size");
		return false;
	}

	const std::string attrUserName{ reinterpret_cast<const char*>(attrUserNamePtr), attrUserNameLen };
	if (attrUserName != username) {
		LOG(SRTC_LOG_E, "Request verification failed: username does not match");
		return false;
	}

	// Signature based on password
	uint16_t attrIntegrityLen = { 0 };
	const auto attrIntegrityPtr = stun_message_find(msg, stun::STUN_ATTRIBUTE_MESSAGE_INTEGRITY, &attrIntegrityLen);

	if (!attrIntegrityPtr || attrIntegrityLen != 20) {
		LOG(SRTC_LOG_E, "Request verification failed: no signature or invalid size");
		return false;
	}

	const auto sha1Message = reinterpret_cast<const uint8_t*>(attrIntegrityPtr);
	uint8_t sha1Calculated[20];

	stun::stun_sha1(msg->buffer,
					sha1Message + 20 - msg->buffer,
					sha1Message - msg->buffer,
					sha1Calculated,
					reinterpret_cast<const uint8_t*>(password.data()),
					password.size(),
					false);

	if (std::memcmp(sha1Calculated, attrIntegrityPtr, 20) != 0) {
		LOG(SRTC_LOG_E, "Request verification failed: signature does not match");
		return false;
	}

	// Success
	return true;
}

bool IceAgent::verifyResponseMessage(stun::StunMessage* msg, const std::string& password)
{
	// Fingerprint
	uint16_t attrFingerprintLen = { 0 };
	auto attrFingerprintPtr = stun::stun_message_find(msg, stun::STUN_ATTRIBUTE_FINGERPRINT, &attrFingerprintLen);

	if (!attrFingerprintPtr || attrFingerprintLen != 4) {
		LOG(SRTC_LOG_E, "Response verification failed: no fingerprint or invalid size");
		return false;
	}

	uint32_t fingerprintMessage;
	std::memcpy(&fingerprintMessage, attrFingerprintPtr, 4);

	const auto fingerprintCalculated = stun::stun_fingerprint(msg->buffer, stun_message_length(msg), false);

	if (fingerprintMessage != fingerprintCalculated) {
		LOG(SRTC_LOG_E, "Response verification failed: fingerprint does not match");
		return false;
	}

	// Signature based on password
	uint16_t attrIntegrityLen = { 0 };
	const auto attrIntegrityPtr =
		stun::stun_message_find(msg, stun::STUN_ATTRIBUTE_MESSAGE_INTEGRITY, &attrIntegrityLen);

	if (!attrIntegrityPtr || attrIntegrityLen != 20) {
		LOG(SRTC_LOG_E, "Response verification failed: no signature or invalid size");
		return false;
	}

	const auto sha1Message = reinterpret_cast<const uint8_t*>(attrIntegrityPtr);
	uint8_t sha1Calculated[20];

	stun::stun_sha1(msg->buffer,
					sha1Message + 20 - msg->buffer,
					sha1Message - msg->buffer,
					sha1Calculated,
					reinterpret_cast<const uint8_t*>(password.data()),
					password.size(),
					false);

	if (std::memcmp(sha1Calculated, attrIntegrityPtr, 20) != 0) {
		LOG(SRTC_LOG_E, "Response verification failed: signature does not match");
		return false;
	}

	// Success
	return true;
}

} // namespace srtc

// Some code in this file is based on Cisco's libSRTP
// https://github.com/cisco/libsrtp
// Copyright (c) 2001-2017 Cisco Systems, Inc.
// All rights reserved.
// Neither the name of the Cisco Systems, Inc. nor the names of its
// contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.

#ifdef _WIN32
#include "srtc/srtc.h"
#include <wincrypt.h>
#undef X509_NAME
#undef X509_EXTENSIONS
#undef PKCS7_SIGNER_INFO
#endif

#include "srtc/logging.h"
#include "srtc/srtp_connection.h"
#include "srtc/srtp_crypto.h"
#include "srtc/util.h"

#include <mutex>

#include <openssl/srtp.h>
#include <openssl/ssl.h>

#include <cassert>
#include <cstring>
#include <iostream>

#define LOG(level, ...) srtc::log(level, "SrtpConnection", __VA_ARGS__)

namespace srtc
{

const char* const SrtpConnection::kSrtpCipherList =
	"SRTP_AEAD_AES_128_GCM:SRTP_AEAD_AES_256_GCM:SRTP_AES128_CM_SHA1_80:SRTP_AES128_CM_SHA1_32";

std::pair<std::shared_ptr<SrtpConnection>, Error> SrtpConnection::create(SSL* dtls_ssl, bool isSetupActive)
{
	// https://stackoverflow.com/questions/22692109/webrtc-srtp-decryption

	const auto srtpProfileName = SSL_get_selected_srtp_profile(dtls_ssl);
	if (srtpProfileName == nullptr) {
		return { nullptr, { Error::Code::InvalidData, "Cannot get SRTP profile from OpenSSL " } };
	}

	LOG(SRTC_LOG_V, "Connected with %s cipher", srtpProfileName->name);

	size_t srtpKeySize, srtpSaltSize;

	switch (srtpProfileName->id) {
	case SRTP_AEAD_AES_256_GCM:
		srtpKeySize = 32;
		srtpSaltSize = 12;
		break;
	case SRTP_AEAD_AES_128_GCM:
		srtpKeySize = 16;
		srtpSaltSize = 12;
		break;
	case SRTP_AES128_CM_SHA1_80:
		srtpKeySize = 16;
		srtpSaltSize = 14;
		break;
	case SRTP_AES128_CM_SHA1_32:
		srtpKeySize = 16;
		srtpSaltSize = 14;
		break;
	default:
		return { nullptr, { Error::Code::InvalidData, "Unsupported SRTP profile" } };
	}

	const auto srtpKeyPlusSaltSize = srtpKeySize + srtpSaltSize;
	const auto material = ByteBuffer{ srtpKeyPlusSaltSize * 2 };

	std::string label = "EXTRACTOR-dtls_srtp";
	SSL_export_keying_material(
		dtls_ssl, material.data(), srtpKeyPlusSaltSize * 2, label.data(), label.size(), nullptr, 0, 0);

	const auto srtpClientKey = material.data();
	const auto srtpServerKey = srtpClientKey + srtpKeySize;
	const auto srtpClientSalt = srtpServerKey + srtpKeySize;
	const auto srtpServerSalt = srtpClientSalt + srtpSaltSize;

	CryptoBytes sendMasterKey, sendMasterSalt;
	CryptoBytes receiveMasterKey, receiveMasterSalt;

	if (isSetupActive) {
		sendMasterKey.assign(srtpServerKey, srtpKeySize);
		sendMasterSalt.assign(srtpServerSalt, srtpSaltSize);

		receiveMasterKey.assign(srtpClientKey, srtpKeySize);
		receiveMasterSalt.assign(srtpClientSalt, srtpSaltSize);
	} else {
		sendMasterKey.assign(srtpClientKey, srtpKeySize);
		sendMasterSalt.assign(srtpClientSalt, srtpSaltSize);

		receiveMasterKey.assign(srtpServerKey, srtpKeySize);
		receiveMasterSalt.assign(srtpServerSalt, srtpSaltSize);
	}

	const auto [crypto, error] =
		SrtpCrypto::create(srtpProfileName->id, sendMasterKey, sendMasterSalt, receiveMasterKey, receiveMasterSalt);
	if (error.isError()) {
		return { nullptr, error };
	}

	const auto conn = std::make_shared<SrtpConnection>(crypto, isSetupActive, srtpProfileName->id);
	return { conn, Error::OK };
}

SrtpConnection::~SrtpConnection() = default;

size_t SrtpConnection::getMediaProtectionOverhead() const
{
	return mCrypto->getMediaProtectionOverhead();
}

bool SrtpConnection::protectOutgoingControl(const ByteBuffer& packetData, uint32_t sequence, ByteBuffer& output)
{
	if (packetData.size() < 4 + 4) {
		// 4 byte header
		// 4 byte SSRC
		LOG(SRTC_LOG_E, "Outgoing RTCP packet is too small");
		return false;
	}

	if (!mCrypto->protectSendRtcp(packetData, sequence, output)) {
		return false;
	}

	return true;
}

bool SrtpConnection::protectOutgoingMedia(const ByteBuffer& packetData, uint32_t rollover, ByteBuffer& output)
{
	if (packetData.size() < 4 + 4 + 4) {
		LOG(SRTC_LOG_E, "Outgoing RTP packet is too small");
		return false;
	}

	return mCrypto->protectSendRtp(packetData, rollover, output);
}

bool SrtpConnection::unprotectIncomingControl(const ByteBuffer& packetData, ByteBuffer& output)
{
	if (packetData.size() < 4 + 4 + 4) {
		// 4 byte header
		// 4 byte SSRC
		// 4 byte trailer
		LOG(SRTC_LOG_E, "Incoming RTCP packet is too small");
		return false;
	}

	const ChannelKey key = { ntohl(*reinterpret_cast<const uint32_t*>(packetData.data() + 4)), 0 };
	auto& channelValue = ensureSrtpChannel(mSrtpInMap, key, std::numeric_limits<uint32_t>::max());

	uint32_t sequenceNumber;
	if (!getRtcpSequenceNumber(packetData, sequenceNumber)) {
		LOG(SRTC_LOG_E, "Error getting sequence number from incoming RTCP packet");
		return false;
	}

	if (!channelValue.replayProtection->canProceed(sequenceNumber)) {
		LOG(SRTC_LOG_E, "Replay protection says we can't proceed with RTCP packet seq = %u", sequenceNumber);
		return false;
	}

	if (!mCrypto->unprotectReceiveRtcp(packetData, output)) {
		return false;
	}

	const auto outputSize = output.size();
	if (outputSize < 4 + 4) {
		// 4 byte header
		// 4 byte SSRC
		LOG(SRTC_LOG_E, "Incoming RTCP packet is too small after unprotecting");
		return false;
	}

	channelValue.replayProtection->set(sequenceNumber);
	return true;
}

SrtpConnection::SrtpConnection(const std::shared_ptr<SrtpCrypto>& crypto, bool isSetupActive, unsigned long profileId)
	: mCrypto(crypto)
	, mProfileId(profileId)
{
}

SrtpConnection::ChannelValue& SrtpConnection::ensureSrtpChannel(ChannelMap& map,
																const ChannelKey& key,
																uint32_t maxPossibleValueForReplayProtection)
{
	const auto iter = map.find(key);
	if (iter != map.end()) {
		return iter->second;
	}

	auto replayProtection = maxPossibleValueForReplayProtection != 0
								? std::make_unique<ReplayProtection>(maxPossibleValueForReplayProtection, 2048)
								: nullptr;
	const auto result = map.insert({ key, ChannelValue{ std::move(replayProtection) } });
	return result.first->second;
}

bool SrtpConnection::getRtcpSequenceNumber(const ByteBuffer& packet, uint32_t& outSequenceNumber) const
{
	size_t offetFromEnd;
	switch (mProfileId) {
	case SRTP_AEAD_AES_256_GCM:
	case SRTP_AEAD_AES_128_GCM:
		// 4 byte RTCP header
		// 4 byte SSRC
		// ... ciphertext
		// 16 byte AES GCM tag
		// 4 byte SRTCP index
		offetFromEnd = 4;
		break;
	case SRTP_AES128_CM_SHA1_80:
	case SRTP_AES128_CM_SHA1_32:
		// 4 byte RTCP header
		// 4 byte SSRC
		// ... ciphertext
		// 4 byte SRTCP index
		// 10 byte HMAC-SHA1 digest
		offetFromEnd = 4 + 10;
		break;
	default:
		assert(false);
		return false;
	}

	const auto packetSize = packet.size();
	if (packetSize < offetFromEnd + 8) {
		return false;
	}

	const auto packetData = packet.data();
	const auto value = ntohl(*reinterpret_cast<const uint32_t*>(packetData + packetSize - offetFromEnd));
	// Clear the encryption bit
	outSequenceNumber = value & ~0x80000000u;
	return true;
}

} // namespace srtc

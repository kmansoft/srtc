#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/error.h"
#include "srtc/replay_protection.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/srtp_util.h"

#include <memory>
#include <unordered_map>
#include <optional>

struct ssl_st;

namespace srtc
{

class SrtpCrypto;

class SrtpConnection
{
public:
	static const char* const kSrtpCipherList;

	static std::pair<std::shared_ptr<SrtpConnection>, Error> create(ssl_st* dtls_ssl, bool isSetupActive);
	~SrtpConnection();

	void onPeerConnected();

	[[nodiscard]] size_t getMediaProtectionOverhead() const;

	// Returns false on error
	bool protectSendControl(const ByteBuffer& packetData, uint32_t sequence, ByteBuffer& output);

	// Returns false on error
	bool protectSendMedia(const ByteBuffer& packetData, uint32_t rollover, ByteBuffer& output);

	// Returns false on error
	bool unprotectReceiveControl(const ByteBuffer& packetData, ByteBuffer& output);

	// Returns false on error
	bool unprotectReceiveMedia(const ByteBuffer& packetData, ByteBuffer& output);

	// Implementation
	SrtpConnection(const std::shared_ptr<SrtpCrypto>& crypto, bool isSetupActive, unsigned long profileId);

private:
	const std::shared_ptr<SrtpCrypto> mCrypto;
	const unsigned long mProfileId;

	struct ChannelKey {
		uint32_t ssrc;
		uint8_t payloadId;
	};

	struct hash_channel_key {
		std::size_t operator()(const ChannelKey& key) const
		{
			return key.ssrc ^ key.payloadId;
		}
	};

	struct equal_to_channel_key {
		bool operator()(const ChannelKey& lhs, const ChannelKey& rhs) const
		{
			return lhs.ssrc == rhs.ssrc && lhs.payloadId == rhs.payloadId;
		}
	};

	struct ChannelValue {
		std::unique_ptr<ReplayProtection> replayProtection;
		uint32_t rolloverCount = { 0 };
		std::optional<uint16_t> lastSequence16;
	};

	using ChannelMap = std::unordered_map<ChannelKey, ChannelValue, hash_channel_key, equal_to_channel_key>;

	ChannelMap mSrtpInMap;
	ChannelMap mSrtpOutMap;

	ChannelValue& ensureSrtpChannel(ChannelMap& map,
									const ChannelKey& key,
									uint32_t maxPossibleValueForReplayProtection);

	bool getControlSequenceNumber(const ByteBuffer& packet, uint32_t& outSequenceNumber) const;
	bool getMediaSequenceNumber(const ByteBuffer& packet, uint16_t& outSequenceNumber) const;
};

} // namespace srtc

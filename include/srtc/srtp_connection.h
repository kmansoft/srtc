#pragma once

#include "srtc/error.h"
#include "srtc/byte_buffer.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/replay_protection.h"
#include "srtc/srtp_util.h"

#include <memory>
#include <unordered_map>

struct ssl_st;

namespace srtc {

class SrtpCrypto;

class SrtpConnection {
public:
    static const char* const kSrtpCipherList;

    static std::pair<std::shared_ptr<SrtpConnection>, Error> create(ssl_st* dtls_ssl, bool isSetupActive);
    ~SrtpConnection();

    // Returns 0 on error
    bool protectOutgoing(const ByteBuffer& packetData,
                         uint32_t rollover,
                         ByteBuffer& output);

    // Returns false on error
    bool unprotectIncomingControl(const ByteBuffer& packetData,
                                  ByteBuffer& output);

    // Implementation
    SrtpConnection(const std::shared_ptr<SrtpCrypto>& crypto,
                   bool isSetupActive,
                   uint16_t profileId);

private:
    const std::shared_ptr<SrtpCrypto> mCrypto;
    const bool mIsSetupActive;
    const uint16_t mProfileId;

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
        bool operator()(const ChannelKey& lhs,
                        const ChannelKey& rhs) const
        {
            return lhs.ssrc == rhs.ssrc && lhs.payloadId == rhs.payloadId;
        }
    };

    struct ChannelValue {
        std::unique_ptr<ReplayProtection> replayProtection;
    };

    using ChannelMap = std::unordered_map<ChannelKey, ChannelValue, hash_channel_key, equal_to_channel_key>;

    ChannelMap mSrtpInMap;
    ChannelMap mSrtpOutMap;

    ChannelValue& ensureSrtpChannel(ChannelMap& map,
                                    const ChannelKey& key,
                                    uint32_t maxPossibleValueForReplayProtection);

    bool getRtcpSequenceNumber(const ByteBuffer& packet,
                               uint32_t& outSequenceNumber) const;
};

}

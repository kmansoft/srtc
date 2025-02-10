#pragma once

#include "srtc/error.h"
#include "srtc/byte_buffer.h"
#include "srtc/rtp_packet_source.h"

#include <srtp.h>
#include <openssl/ssl.h>

#include <memory>
#include <unordered_map>

namespace srtc {

class SrtpConnection {
public:
    static std::pair<std::shared_ptr<SrtpConnection>, Error> create(SSL* dtls_ssl, bool isSetupActive);
    ~SrtpConnection();

    // Returns 0 on error
    size_t protectOutgoing(ByteBuffer& packetData);

    // Returns 0 on error
    size_t unprotectIncomingControl(ByteBuffer& packetData);

    // Implementation
    SrtpConnection(ByteBuffer&& srtpClientKeyBuf,
                   ByteBuffer&& srtpServerKeyBuf,
                   size_t keySize,
                   bool isSetupActive,
                   srtp_profile_t profile);

private:
    const ByteBuffer mSrtpClientKeyBuf;
    const ByteBuffer mSrtpServerKeyBuf;
    const size_t mKeySize;
    const bool mIsSetupActive;
    const srtp_profile_t mProfile;

    srtp_policy_t mSrtpReceivePolicy = { };
    srtp_policy_t mSrtpSendPolicy = { };

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

    using ChannelMap = std::unordered_map<ChannelKey, srtp_t, hash_channel_key, equal_to_channel_key>;

    ChannelMap mSrtpInMap;
    ChannelMap mSrtpOutMap;

    srtp_t ensureSrtpChannel(ChannelMap& map,
                             const ChannelKey& key,
                             const srtp_policy_t* policy);
};

}

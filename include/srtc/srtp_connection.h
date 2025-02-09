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
    size_t protectOutgoing(const std::shared_ptr<RtpPacketSource>& source,
                           ByteBuffer& packetData);

    // Returns 0 on error
    size_t unprotectIncomingControl(ByteBuffer& packetData);

    // Implementation
    SrtpConnection(ByteBuffer&& srtpClientKeyBuf,
                   ByteBuffer&& srtpServerKeyBuf,
                   bool isSetupActive,
                   srtp_profile_t profile);

private:
    const ByteBuffer mSrtpClientKeyBuf;
    const ByteBuffer mSrtpServerKeyBuf;

    srtp_policy_t mSrtpReceivePolicy = { };
    srtp_policy_t mSrtpSendPolicy = { };

    struct hash_packet_source {
        std::size_t operator()(const std::shared_ptr<srtc::RtpPacketSource>& source) const
        {
            return std::hash<uint32_t>()(source->getSSRC() ^ source->getPayloadId());
        }
    };

    struct equal_to_packet_source {
        bool operator()(const std::shared_ptr<srtc::RtpPacketSource>& lhs,
                        const std::shared_ptr<srtc::RtpPacketSource>& rhs) const
        {
            if (lhs.get() == rhs.get()) {
                return true;
            }

            return lhs->getSSRC() == rhs->getSSRC() && lhs->getPayloadId() == rhs->getPayloadId();
        }
    };


    srtp_t mSrtpControlIn = { nullptr };

    std::unordered_map<std::shared_ptr<RtpPacketSource>, srtp_t,
        hash_packet_source, equal_to_packet_source> mSrtpOutMap;

};

}

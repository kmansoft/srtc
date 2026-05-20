#pragma once

#include "sctp/sctp_packet.h"
#include "srtc/byte_buffer.h"
#include "srtc/random_generator.h"
#include "srtc/scheduler.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace srtc::sctp
{

class SctpSessionListener;

class SctpSession
{
public:
    SctpSession(const std::shared_ptr<RealScheduler>& scheduler,
                SctpSessionListener* listener,
                uint16_t localPort,
                uint16_t remotePort,
                uint32_t maxMessageSize,
                bool isSetupActive,
                const std::vector<std::string>& dataChannels);

    void start();

    void onReceiveData(const ByteBuffer& data);

    enum class State {
        New,
        CookieWait,
        CookieEchoed,
        Established,
    };

private:
    SctpSessionListener* const mListener;
    const uint16_t mLocalPort;
    const uint16_t mRemotePort;
    const uint32_t mMaxMessageSize;
    const bool mIsSetupActive;
    const std::vector<std::string> mDataChannels;

    RandomGenerator<uint32_t> mRandom;
    State mState;
    uint32_t mInitiateTag;   // our tag — peer puts this in verificationTag of packets it sends us
    uint32_t mInitialTsn;    // our DATA chunk sequence counter start
    uint32_t mPeerTag;       // peer's tag — we put this in verificationTag of packets we send
    std::array<uint8_t, 16> mHmacKey;

    void onReceiveInit(const SctpPacket::Chunk& chunk);
    void onReceiveCookieEcho(const SctpPacket::Chunk& chunk);

    ScopedScheduler mScheduler;
};

} // namespace srtc::sctp
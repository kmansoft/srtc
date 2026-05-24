#pragma once

#include "sctp/sctp_packet.h"
#include "srtc/byte_buffer.h"
#include "srtc/random_generator.h"
#include "srtc/scheduler.h"

#include <array>
#include <cstdint>
#include <list>
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

    ~SctpSession();

    void start();

    void onReceiveData(const ByteBuffer& data);

private:
    SctpSessionListener* const mListener;
    const uint16_t mLocalPort;
    const uint16_t mRemotePort;
    const uint32_t mMaxMessageSize;
    const bool mIsRemoteSetupActive; // true = remote is DTLS client, we are passive, use odd stream IDs

    enum class DataChannelState {
        kNone,
        kOpening,
        kOpen,
    };

    struct DataChannel {
        const std::string label;
        const uint16_t streamId;
        DataChannelState state;
        std::weak_ptr<Task> taskT1Open;

        DataChannel(const std::string& label, uint16_t streamId)
            : label(label)
            , streamId(streamId)
            , state(DataChannelState::kNone)
        {
        }
    };
    std::list<DataChannel> mDataChannels;

    RandomGenerator<uint32_t> mRandom;

    enum class State {
        New,
        CookieWait,
        CookieEchoed,
        Established,
    };

    State mState;
    uint32_t mInitiateTag;       // our tag — peer puts this in verificationTag of packets it sends us
    uint32_t mInitialTsn;        // our DATA chunk sequence counter start
    uint32_t mPeerTag;           // peer's tag — we put this in verificationTag of packets we send
    uint16_t mPeerOutStreams;     // number of outbound streams the peer supports
    uint16_t mPeerInStreams;      // number of inbound streams the peer supports
    uint32_t mPeerRwnd;          // peer's receive window
    uint32_t mCurrentTsn;        // next TSN to use when sending DATA chunks
    uint32_t mPeerCumulativeTsn; // highest consecutive TSN received from peer
    std::array<uint8_t, 16> mHmacKey;
    ByteBuffer mCookieEchoPacket;
    std::weak_ptr<Task> mTaskT1Init;
    std::weak_ptr<Task> mTaskT1Cookie;

    void sendInit(unsigned iteration);
    void sendCookieEcho(unsigned iteration);
    void onReceiveInit(const SctpPacket::Chunk& chunk);
    void onReceiveCookieEcho(const SctpPacket::Chunk& chunk);
    void onAssociationEstablished();
    void sendDataChannelOpen(DataChannel& channel, unsigned iteration);
    void onReceiveDataChunk(const SctpPacket::Chunk& chunk);
    void sendSack();

    ScopedScheduler mScheduler;
};

} // namespace srtc::sctp
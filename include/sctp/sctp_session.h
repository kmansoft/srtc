#pragma once

#include "sctp/data_channel_receive_buffer.h"
#include "sctp/sctp_packet.h"
#include "srtc/byte_buffer.h"
#include "srtc/data_channel_message.h"
#include "srtc/random_generator.h"
#include "srtc/scheduler.h"

#include <array>
#include <cstdint>
#include <list>
#include <set>
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
    [[nodiscard]] bool isChannelOpen(const std::string& label) const;
    void send(DataChannelMessage&& message);

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
        const bool unordered;
        DataChannelState state;
        DataChannelReceiveBuffer receiveBuffer;
        uint16_t sendSsn = 0;

        DataChannel(const std::string& label, uint16_t streamId, bool unordered)
            : label(label)
            , streamId(streamId)
            , unordered(unordered)
            , state(DataChannelState::kNone)
        {
        }
    };
    std::list<DataChannel> mDataChannels;

    struct SentChunk {
        uint32_t tsn;
        uint8_t flags;
        size_t payloadSize;
        ByteBuffer body; // TSN(4) + streamId(2) + ssn(2) + ppid(4) + payload
    };
    std::list<SentChunk> mSentChunks;
    std::list<DataChannelMessage> mPendingSend;
    uint32_t mFlightSize = 0;

    RandomGenerator<uint32_t> mRandom;

    enum class State {
        New,
        CookieWait,
        CookieEchoed,
        Established,
    };

    State mState;
    uint32_t mInitiateTag;                  // our tag — peer puts this in verificationTag of packets it sends us
    uint32_t mInitialTsn;                   // our DATA chunk sequence counter start
    uint32_t mPeerTag;                      // peer's tag — we put this in verificationTag of packets we send
    uint16_t mPeerOutStreams;               // number of outbound streams the peer supports
    uint16_t mPeerInStreams;                // number of inbound streams the peer supports
    uint32_t mPeerRwnd;                     // peer's receive window
    uint32_t mCurrentTsn;                   // next TSN to use when sending DATA chunks
    uint32_t mPeerCumulativeTsn;            // highest consecutive TSN received from peer
    std::set<uint32_t> mPeerOutOfOrderTsns; // received but not yet consecutive
    std::array<uint8_t, 16> mHmacKey;
    ByteBuffer mCookieEchoPacket;
    std::weak_ptr<Task> mTaskT1Init;
    std::weak_ptr<Task> mTaskT1Cookie;

    void sendInit(unsigned iteration);
    void sendCookieEcho(unsigned iteration);
    void onReceiveInit(const SctpPacket::Chunk& chunk);
    void onReceiveCookieEcho(const SctpPacket::Chunk& chunk);
    void onAssociationEstablished();
    void sendDataChannelOpen(DataChannel& channel);
    void onReceiveDataChunk(const SctpPacket::Chunk& chunk);
    void onReceiveReconfig(const SctpPacket::Chunk& chunk);
    void onReceiveSack(const SctpPacket::Chunk& chunk);
    void sendSack();

    void sendMessageNow(DataChannel& channel, DataChannelMessage&& message);
    void transmitPending();
    void retransmitOldest();
    void startT3Rtx();
    void stopT3Rtx();

    std::weak_ptr<Task> mTaskT3Rtx;
    ScopedScheduler mScheduler;
};

} // namespace srtc::sctp
#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/peer_candidate_listener.h"
#include "srtc/random_generator.h"
#include "srtc/scheduler.h"
#include "srtc/socket.h"
#include "srtc/srtc.h"

#include <list>
#include <memory>
#include <mutex>
#include <vector>

struct ssl_st;
struct ssl_ctx_st;
struct bio_st;
struct bio_method_st;

namespace srtc
{

class Error;
class PeerCandidate;
class Track;
class Packetizer;
class SdpOffer;
class SdpAnswer;
class IceAgent;
class SendHistory;
class SrtpConnection;
class RtcpPacket;
class EventLoop;
class RtpExtensionSourceSimulcast;
class RtpExtensionSourceTWCC;
class SendPacer;

struct PublishConnectionStats;

class PeerCandidate final
{
public:
    PeerCandidate(PeerCandidateListener* listener,
                  const std::shared_ptr<SdpOffer>& offer,
                  const std::shared_ptr<SdpAnswer>& answer,
                  const std::shared_ptr<RealScheduler>& scheduler,
                  const Host& host,
                  const std::shared_ptr<EventLoop>& eventLoop,
                  const Scheduler::Delay& startDelay);
    ~PeerCandidate();

    void receiveFromSocket();

    struct FrameToSend {
        std::shared_ptr<Track> track;
        std::shared_ptr<Packetizer> packetizer;
        ByteBuffer buf;              // possibly empty
        std::vector<ByteBuffer> csd; // possibly empty
    };
    void addSendFrame(FrameToSend&& frame);

	[[nodiscard]] int getTimeoutMillis(int defaultValue) const;
    void run();

    void sendRtcpPacket(const std::shared_ptr<RtcpPacket>& packet);

    void updatePublishConnectionStats(PublishConnectionStats& stats) const;

private:
    void startConnecting();
    void addSendRaw(ByteBuffer&& buf);

    void onReceivedStunMessage(const Socket::ReceivedData& data);
    void onReceivedDtlsMessage(ByteBuffer&& buf);
    void onReceivedRtcMessage(ByteBuffer&& buf);
    void onReceivedRtcMessageUnprotected(const ByteBuffer& buf);

    void onReceivedRtcMessage_205_1(uint32_t ssrc, ByteReader& rtcpReader);
    void onReceivedRtcMessage_205_15(uint32_t ssrc, ByteReader& rtcpReader);
    void forgetExpiredStunRequests();

    PeerCandidateListener* const mListener;
    const std::shared_ptr<SdpOffer> mOffer;
    const std::shared_ptr<SdpAnswer> mAnswer;
    const Host mHost;
    const std::shared_ptr<EventLoop> mEventLoop;
    const std::shared_ptr<Socket> mSocket;
    const std::shared_ptr<IceAgent> mIceAgent;
    const std::unique_ptr<uint8_t[]> mIceMessageBuffer;
    const std::shared_ptr<SendHistory> mSendHistory;
    const uint32_t mUniqueId;
    const uint8_t mVideoExtMediaId;
    const uint8_t mVideoExtStreamId;
    const uint8_t mVideoExtRepairedStreamId;
    const uint8_t mVideoExtGoogleVLA;
    const std::shared_ptr<RtpExtensionSourceSimulcast> mExtensionSourceSimulcast;
    const std::shared_ptr<RtpExtensionSourceTWCC> mExtensionSourceTWCC;

    std::shared_ptr<SrtpConnection> mSrtp;
	std::shared_ptr<SendPacer> mSendPacer;

    std::list<ByteBuffer> mDtlsReceiveQueue;

    std::list<Socket::ReceivedData> mRawReceiveQueue;

    std::list<ByteBuffer> mRawSendQueue;
    std::list<FrameToSend> mFrameSendQueue;

    bool mSentUseCandidate = { false };

    // DTLS
    enum class DtlsState {
        Inactive,
        Activating,
        Failed,
        Completed
    };

    ssl_ctx_st* mDtlsCtx = {};
    ssl_st* mDtlsSsl = {};
    bio_st* mDtlsBio = {};
    DtlsState mDtlsState = { DtlsState::Inactive };

    // OpenSSL BIO
    static int dgram_read(struct bio_st* b, char* out, int outl);
    static int dgram_write(struct bio_st* b, const char* in, int inl);
    static long dgram_ctrl(struct bio_st* b, int cmd, long num, void* ptr);
    static int dgram_free(struct bio_st* b);

    static std::once_flag dgram_once;
    static struct bio_method_st* dgram_method;

    static struct bio_st* BIO_new_dgram(PeerCandidate* pc);

    void freeDTLS();

    // State
    void emitOnConnecting();
    void emitOnIceConnected();
    void emitOnDtlsConnected();
    void emitOnFailedToConnect(const Error& error);
    void emitOnLostConnection(const Error& error);

    // Sending STUN requests and responses
    void sendStunBindingRequest();
    void sendStunBindingResponse();

    // Timeouts
    void updateConnectionLostTimeout();
    void onConnectionLostTimeout();
    void updateKeepAliveTimeout();
    void onKeepAliveTimeout();

    std::chrono::steady_clock::time_point mLastSendTime;
    std::chrono::steady_clock::time_point mLastReceiveTime;

    // Scheduler and tasks
    std::weak_ptr<Task> mTaskConnectTimeout;
    std::weak_ptr<Task> mTaskSendStunConnectRequest;
    std::weak_ptr<Task> mTaskSendStunConnectResponse;
    std::weak_ptr<Task> mTaskConnectionLostTimeout;
    std::weak_ptr<Task> mTaskExpireStunRequests;
    std::weak_ptr<Task> mTaskKeepAliveTimeout;

    ScopedScheduler mScheduler;
};

} // namespace srtc

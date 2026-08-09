#pragma once

#include "receiver_reference_time_reports_history.h"
#include "srtc/byte_buffer.h"
#include "srtc/data_channel_message.h"
#include "srtc/peer_candidate_listener.h"
#include "srtc/random_generator.h"
#include "srtc/receiver_reference_time_report.h"
#include "srtc/scheduler.h"
#include "srtc/sctp_session_listener.h"
#include "srtc/socket.h"
#include "srtc/srtc.h"
#include "srtc/util.h"

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

struct DataChannelMessage;

class Error;
class PeerCandidate;
class Track;
class Packetizer;
class SdpOffer;
class SdpAnswer;
class IceAgent;
class SendRtpHistory;
class SrtpConnection;
class RtcpPacket;
class EventLoop;
class RtpExtensionSourceSimulcast;
class RtpExtensionSourceTWCC;
class RtpResponderTWCC;
class SendPacer;
class SenderReportsHistory;
class ReceiverReferenceTimeReportsHistory;
class RtcpPacketSource;

struct PublishConnectionStats;

namespace sctp
{
class SctpSession;
}

class PeerCandidate final : sctp::SctpSessionListener
{
public:
    PeerCandidate(PeerCandidateListener* listener,
                  Direction direction,
                  const std::shared_ptr<SdpOffer>& offer,
                  const std::shared_ptr<SdpAnswer>& answer,
                  uint32_t dataChannelMaxMessageSize,
                  const std::shared_ptr<RealScheduler>& scheduler,
                  const Host& host,
                  const std::shared_ptr<EventLoop>& eventLoop,
                  const Scheduler::Delay& startDelay);
    ~PeerCandidate() override;

    void receiveFromSocket();

    struct FrameToSend {
        int64_t pts_usec;
        std::shared_ptr<Track> track;
        std::shared_ptr<Packetizer> packetizer;
        ByteBuffer buf;              // possibly empty
        std::vector<ByteBuffer> csd; // possibly empty
    };
    void addSendFrame(FrameToSend&& frame);

    [[nodiscard]] int getTimeoutMillis(int defaultValue) const;
    void run();

    void sendPublishReports();
    void sendSubscribeReports();
    void sendPeriodicPictureLossIndicators();
    void sendPictureLossIndicator(const std::shared_ptr<Track>& track);
    void sendNacks(const std::shared_ptr<Track>& track, const std::vector<uint16_t>& nackList);

    void updatePublishConnectionStats(PublishConnectionStats& stats) const;
    void updateSubscribeConnectionStats(SubscribeConnectionStats& stats) const;

    [[nodiscard]] std::optional<float> getIceRtt() const;

    // SCTP listener
    void onSctpSendPacket(const ByteBuffer& packet) override;
    void onSctpDataChannelOpen(const std::string& label) override;
    void onSctpDataChannelText(const std::string& label, const std::string& text) override;
    void onSctpDataChannelBinary(const std::string& label, const ByteBuffer& data) override;
    void onSctpDataChannelClose(const std::string& label) override;

    // Sending data channel messages
    void sendDataChannelMessage(DataChannelMessage&& message);

private:
    void startConnecting();
    void addSendRaw(ByteBuffer&& buf);
    void flushSendRaw();

    void onReceivedStunMessage(const Socket::ReceivedData& data);
    void onReceivedDtlsMessage(ByteBuffer&& buf);
    void onReceivedRtcMessage(ByteBuffer&& buf);

    void onReceivedControlPacket(const std::shared_ptr<RtcpPacket>& packet);
    void onReceivedMediaPacket(const std::shared_ptr<RtpPacket>& packet);

    void onReceivedControlMessage_SR(uint32_t ssrc, ByteReader& rtcpReader);
    void onReceivedControlMessage_RR(ByteReader& rtcpReader);
    void onReceivedControlMessage_NACK(uint32_t ssrc, ByteReader& rtcpReader);
    void onReceivedControlMessage_TWCC(uint32_t ssrc, ByteReader& rtcpReader);
    void onReceivedControlMessage_PLI();
    void onReceivedControlMessage_FIR();
    void onReceivedControlMessage_RRTR(uint32_t ssrc, ByteReader& rtcpReader);
    void onReceivedControlMessage_DLRR(uint32_t ssrc, ByteReader& rtcpReader);

    void forgetExpiredStunRequests();

    void sendRtcpPacket(const std::shared_ptr<Track>& track, const std::shared_ptr<RtcpPacket>& packet);
    void sendRtcpPacket(const std::shared_ptr<RtcpPacketSource>& track, const std::shared_ptr<RtcpPacket>& packet);

    [[nodiscard]] std::shared_ptr<Track> findReceiveTrack(uint32_t ssrc) const;
    [[nodiscard]] std::shared_ptr<Track> findReceiveTrack(ByteBuffer& packet) const;

    PeerCandidateListener* const mListener;

    const Direction mDirection;
    const std::vector<std::shared_ptr<Track>> mTrackList;
    const std::shared_ptr<SdpOffer> mOffer;
    const std::shared_ptr<SdpAnswer> mAnswer;
    const Host mHost;
    const std::shared_ptr<EventLoop> mEventLoop;
    const std::shared_ptr<Socket> mSocket;
    const std::shared_ptr<IceAgent> mIceAgent;
    const std::unique_ptr<uint8_t[]> mIceMessageBuffer;
    const std::shared_ptr<SendRtpHistory> mSendRtpHistory;
    const uint32_t mUniqueId;
    const std::shared_ptr<RtpExtensionSourceSimulcast> mExtensionSourceSimulcast;
    const std::shared_ptr<RtpExtensionSourceTWCC> mExtensionSourceTWCC;
    const std::shared_ptr<RtpResponderTWCC> mResponderTWCC;
    const std::shared_ptr<SenderReportsHistory> mSenderReportsHistory;
    const std::shared_ptr<ReceiverReferenceTimeReportsHistory> mReceiverReferenceTimeReportsHistory;
    const std::shared_ptr<RtcpPacketSource> mControlPacketSource;

    Filter<float> mIceRttFilter;
    Filter<float> mControlRttFilter;

    mutable uint64_t mPrevPublishByteCount = 0;
    mutable std::chrono::steady_clock::time_point mPrevStatsTime = {};

    std::shared_ptr<SrtpConnection> mSrtpConnection;
    std::shared_ptr<SendPacer> mSendPacer;
    std::shared_ptr<sctp::SctpSession> mSctpSession;

    std::list<ByteBuffer> mDtlsReceiveQueue;

    std::list<Socket::ReceivedData> mRawReceiveQueue;

    std::list<ByteBuffer> mRawSendQueue;
    std::list<FrameToSend> mFrameSendQueue;
    std::list<DataChannelMessage> mDataSendQueue;

    std::vector<SimulcastLayer> mSimulcastLayerList;

    std::vector<ReceiverReferenceTimeReport> mOutstandingReceiverReferenceTimeReportList;

    bool mSentUseCandidate;
    bool mIsConnected;

    ByteBuffer mProtectedBuf;

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

    // RTT
    std::optional<float> calculateRtt(const std::chrono::steady_clock::time_point& now) const;

    // State
    void emitOnConnecting();
    void emitOnIceConnected();
    void emitOnDtlsConnected();
    void emitOnFailedToConnect(const Error& error);
    void emitOnDtlsDisconnected(const Error& error);

    void onReceivedFromRemote();

    // Sending STUN requests and responses
    void sendStunBindingRequest(unsigned int iteration);
    void sendStunBindingResponse(unsigned int iteration);

    // Timeouts
    void updateConnectionLostTimeout();
    void onConnectionLostTimeout();
    void sendConnectionRestoreRequest();
    void updateKeepAliveTimeout();
    void onKeepAliveTimeout();

    std::chrono::steady_clock::time_point mLastSendTime;
    std::chrono::steady_clock::time_point mLastReceiveTime;

    // Scheduler and tasks
    std::weak_ptr<Task> mTaskConnectTimeout;
    std::weak_ptr<Task> mTaskSendStunConnectRequest;
    std::weak_ptr<Task> mTaskSendStunConnectResponse;
    std::weak_ptr<Task> mTaskConnectionLostTimeout;
    std::weak_ptr<Task> mTaskConnectionRestoreTimeout;
    std::weak_ptr<Task> mTaskExpireStunRequests;
    std::weak_ptr<Task> mTaskKeepAliveTimeout;

    ScopedScheduler mScheduler;

#ifdef NDEBUG
#else
    struct LosePacketsItem {
        uint32_t ssrc;
        uint16_t seq;

        LosePacketsItem(uint32_t ssrc, uint16_t seq)
            : ssrc(ssrc)
            , seq(seq)
        {
        }
    };

    class LosePacketsHistory
    {
        std::list<LosePacketsItem> history;

    public:
        [[nodiscard]] bool shouldLosePacket(uint32_t ssrc, uint16_t seq)
        {
            if (didLosePacket(ssrc, seq)) {
                return false;
            }

            while (history.size() > 256) {
                history.pop_front();
            }
            history.emplace_back(ssrc, seq);

            return true;
        }

        [[nodiscard]] bool didLosePacket(uint32_t ssrc, uint16_t seq)
        {
            for (const auto& item : history) {
                if (item.ssrc == ssrc && item.seq == seq) {
                    return true;
                }
            }

            return false;
        }
    };

    RandomGenerator<uint32_t> mLosePacketsRandomGenerator;
    LosePacketsHistory mLosePacketHistory;
#endif
};

} // namespace srtc

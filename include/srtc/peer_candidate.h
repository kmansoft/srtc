#pragma once

#include "srtc/srtc.h"
#include "srtc/socket.h"
#include "srtc/byte_buffer.h"
#include "srtc/peer_candidate_listener.h"

#include <list>
#include <memory>
#include <vector>
#include <mutex>

struct ssl_st;
struct ssl_ctx_st;
struct bio_st;
struct bio_method_st;

namespace srtc {

class Error;
class PeerCandidate;
class Track;
class Packetizer;
class SdpOffer;
class SdpAnswer;
class IceAgent;
class SendHistory;
class SrtpConnection;

class PeerCandidate final {
public:
    PeerCandidate(PeerCandidateListener* listener,
                  const std::shared_ptr<SdpOffer>& offer,
                  const std::shared_ptr<SdpAnswer>& answer,
                  const Host& host);
    ~PeerCandidate();

    [[nodiscard]] int getSocketFd() const;

    void receiveFromSocket();

    struct FrameToSend {
        std::shared_ptr<Track> track;
        std::shared_ptr<Packetizer> packetizer;
        ByteBuffer buf;                 // possibly empty
        std::vector<ByteBuffer> csd;    // possibly empty
    };
    void addSendFrame(FrameToSend&& frame);

    void process();

private:
    void addSendRaw(ByteBuffer&& buf);

    void onReceivedStunMessage(const Socket::ReceivedData& data);
    void onReceivedDtlsMessage(ByteBuffer&& buf);
    void onReceivedRtcMessage(ByteBuffer&& buf);
    void onReceivedRtcMessageUnprotected(const ByteBuffer& buf,
                                         size_t unprotectedSize);

    PeerCandidateListener* const mListener;
    const std::shared_ptr<SdpOffer> mOffer;
    const std::shared_ptr<SdpAnswer> mAnswer;
    const Host mHost;
    const std::shared_ptr<Socket> mSocket;
    const std::shared_ptr<IceAgent> mIceAgent;
    const std::unique_ptr<uint8_t[]> mIceMessageBuffer;
    const std::shared_ptr<SendHistory> mSendHistory;

    std::shared_ptr<SrtpConnection> mSrtp;

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
    static int dgram_read(struct bio_st *b, char *out, int outl);
    static int dgram_write(struct bio_st *b, const char *in, int inl);
    static long dgram_ctrl(struct bio_st *b, int cmd, long num, void *ptr);
    static int dgram_free(struct bio_st *b);

    static std::once_flag dgram_once;
    static struct bio_method_st* dgram_method;

    static struct bio_st *BIO_new_dgram(PeerCandidate* pc);

    // State
    void emitOnConnecting();
    void emitOnConnected();
    void emitOnFailed(const Error& error);
};

}

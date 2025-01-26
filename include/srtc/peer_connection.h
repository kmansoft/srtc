#pragma once

#include "srtc/srtc.h"
#include "srtc/byte_buffer.h"
#include "srtc/error.h"

#include <memory>
#include <mutex>
#include <thread>
#include <list>
#include <string>
#include <functional>

struct bio_st;
struct bio_method_st;

namespace srtc {

class SdpAnswer;
class SdpOffer;
class Track;
class Packetizer;
class Scheduler;

class PeerConnection {
public:
    PeerConnection();
    ~PeerConnection();

    void setSdpOffer(const std::shared_ptr<SdpOffer>& offer);
    Error setSdpAnswer(const std::shared_ptr<SdpAnswer>& answer);

    std::shared_ptr<SdpOffer> getSdpOffer() const;
    std::shared_ptr<SdpAnswer> getSdpAnswer() const;

    std::shared_ptr<Track> getVideoTrack() const;
    std::shared_ptr<Track> getAudioTrack() const;

    enum class ConnectionState {
        Inactive = 0,
        Connecting = 1,
        Connected = 2,
        Failed = 100,
        Closed = 200
    };
    using ConnectionStateListener = std::function<void(ConnectionState state)>;
    void setConnectionStateListener(const ConnectionStateListener& listener);

    Error setVideoCodecSpecificData(std::vector<ByteBuffer>& list);
    Error publishVideoFrame(ByteBuffer& buf);
    Error publishAudioFrame(ByteBuffer& buf);

private:
    mutable std::mutex mMutex;

    std::shared_ptr<SdpOffer> mSdpOffer SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<SdpAnswer> mSdpAnswer SRTC_GUARDED_BY(mMutex);

    std::shared_ptr<Track> mVideoTrack SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<Track> mAudioTrack SRTC_GUARDED_BY(mMutex);

    void networkThreadWorkerFunc(std::shared_ptr<SdpOffer> offer,
                                 std::shared_ptr<SdpAnswer> answer);

    void enqueueForSending(ByteBuffer&& buf) SRTC_LOCKS_EXCLUDED(mMutex);

    void setConnectionState(ConnectionState state) SRTC_LOCKS_EXCLUDED(mMutex, mListenerMutex);

    enum class DtlsState {
        Inactive,
        Activating,
        Failed,
        Completed
    };

    struct FrameToSend {
        std::shared_ptr<Track> track;
        std::shared_ptr<Packetizer> packetizer;
        ByteBuffer buf;                 // possibly empty
        std::vector<ByteBuffer> csd;    // possibly empty
    };

    bool mIsQuit SRTC_GUARDED_BY(mMutex) = { false };
    std::thread mThread SRTC_GUARDED_BY(mMutex);

    int mEventHandle SRTC_GUARDED_BY(mMutex) = { -1 };

    std::list<ByteBuffer> mRawSendQueue;
    std::list<FrameToSend> mFrameSendQueue;

    // A queue for incoming DTLS data, routed through a custom BIO
    std::list<ByteBuffer> mDtlsReceiveQueue SRTC_GUARDED_BY(mMutex);

    // Overall connection state and listener
    ConnectionState mConnectionState SRTC_GUARDED_BY(mMutex) = { ConnectionState::Inactive };

    std::mutex mListenerMutex;
    ConnectionStateListener mConnectionStateListener SRTC_GUARDED_BY(mListenerMutex);

    // Packetizers
    std::shared_ptr<Packetizer> mVideoPacketizer SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<Packetizer> mAudioPacketizer SRTC_GUARDED_BY(mMutex);

    // A scheduler for retries
    const std::unique_ptr<Scheduler> mScheduler;

    // OpenSSL BIO
    static int dgram_read(struct bio_st *b, char *out, int outl);
    static int dgram_write(struct bio_st *b, const char *in, int inl);
    static long dgram_ctrl(struct bio_st *b, int cmd, long num, void *ptr);
    static int dgram_free(struct bio_st *b);

    static const struct bio_method_st dgram_method;

    static struct bio_st *BIO_new_dgram(PeerConnection* pc);
};

}

#pragma once

#include "srtc/srtc.h"
#include "srtc/byte_buffer.h"

#include <memory>
#include <mutex>
#include <thread>
#include <list>
#include <string>

struct bio_st;
struct bio_method_st;

namespace srtc {

class SdpAnswer;
class SdpOffer;
class Track;

class PeerConnection {
public:
    PeerConnection();
    ~PeerConnection();

    void setSdpOffer(const std::shared_ptr<SdpOffer>& offer);
    void setSdpAnswer(const std::shared_ptr<SdpAnswer>& answer);

    std::shared_ptr<SdpOffer> getSdpOffer() const;
    std::shared_ptr<SdpAnswer> getSdpAnswer() const;

    std::shared_ptr<Track> getVideoTrack() const;
    std::shared_ptr<Track> getAudioTrack() const;

private:
    mutable std::mutex mMutex;

    std::shared_ptr<SdpOffer> mSdpOffer SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<SdpAnswer> mSdpAnswer SRTC_GUARDED_BY(mMutex);

    std::shared_ptr<Track> mVideoTrack SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<Track> mAudioTrack SRTC_GUARDED_BY(mMutex);

    void networkThreadWorkerFunc(const std::shared_ptr<SdpOffer> offer,
                                 const std::shared_ptr<SdpAnswer> answer,
                                 const Host host);

    void enqueueForSending(ByteBuffer&& buf) SRTC_LOCKS_EXCLUDED(mMutex);

    enum class State {
        Inactive,
        Active,
        Deactivating
    };

    struct ReceivedData {
        ByteBuffer buf;
        anyaddr addr;
        size_t addr_len;
    };

    State mState SRTC_GUARDED_BY(mMutex) = { State::Inactive };
    std::thread mThread SRTC_GUARDED_BY(mMutex);
    Host mDestHost = { };

    int mEventHandle SRTC_GUARDED_BY(mMutex) = { -1 };
    int mSocketHandle SRTC_GUARDED_BY(mMutex) = { -1 };

    std::list<ByteBuffer> mSendQueue;

    // A queue for incoming DTLS data, routed through a custom BIO
    std::list<ByteBuffer> mDtlsReceiveQueue SRTC_GUARDED_BY(mMutex);

    // OpenSSL BIO
    static int dgram_read(struct bio_st *b, char *out, int outl);
    static int dgram_write(struct bio_st *b, const char *in, int inl);
    static long dgram_ctrl(struct bio_st *b, int cmd, long num, void *ptr);
    static int dgram_free(struct bio_st *b);

    static const struct bio_method_st dgram_method;

    static struct bio_st *BIO_new_dgram(PeerConnection* pc);
};

}

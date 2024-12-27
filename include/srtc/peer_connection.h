#pragma once

#include "srtc/srtc.h"
#include "srtc/byte_buffer.h"

#include <memory>
#include <mutex>
#include <thread>
#include <queue>

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

    void networkThreadWorkerFunc();
    void networkThreadDtlsTestFunc(const std::shared_ptr<SdpOffer> offer, const Host host);

    enum class State {
        Inactive,
        Active,
        Deactivating
    };

    State mState SRTC_GUARDED_BY(mMutex) = { State::Inactive };
    std::thread mThread SRTC_GUARDED_BY(mMutex);
    Host mDestHost = { };

    int mEventHandle SRTC_GUARDED_BY(mMutex) = { -1 };
    int mSocketHandle SRTC_GUARDED_BY(mMutex) = { -1 };

    std::queue<std::shared_ptr<ByteBuffer>> mSendQueue;
};

}

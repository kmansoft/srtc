#pragma once

#include "srtc/srtc.h"
#include "srtc/byte_buffer.h"
#include "srtc/error.h"
#include "srtc/peer_candidate_listener.h"
#include "srtc/peer_candidate.h"

#include <memory>
#include <mutex>
#include <thread>
#include <list>
#include <string>
#include <functional>

namespace srtc {

class SdpAnswer;
class SdpOffer;
class Track;
class Packetizer;
class Scheduler;

class PeerConnection final :
        public PeerCandidateListener {
public:
    PeerConnection();
    ~PeerConnection();

    Error setSdpOffer(const std::shared_ptr<SdpOffer>& offer);
    Error setSdpAnswer(const std::shared_ptr<SdpAnswer>& answer);

    std::shared_ptr<SdpOffer> getSdpOffer() const;
    std::shared_ptr<SdpAnswer> getSdpAnswer() const;

    std::shared_ptr<Track> getVideoSingleTrack() const;
    std::vector<std::shared_ptr<Track>> getVideoSimulcastTrackList() const;
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
    Error publishVideoFrame(ByteBuffer&& buf);
    Error publishAudioFrame(ByteBuffer&& buf);

private:
    mutable std::mutex mMutex;

    std::shared_ptr<SdpOffer> mSdpOffer SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<SdpAnswer> mSdpAnswer SRTC_GUARDED_BY(mMutex);

    std::shared_ptr<Track> mVideoSingleTrack SRTC_GUARDED_BY(mMutex);
    std::vector<std::shared_ptr<Track>> mVideoSimulcastTrackList SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<Track> mAudioTrack SRTC_GUARDED_BY(mMutex);

    void networkThreadWorkerFunc(std::shared_ptr<SdpOffer> offer,
                                 std::shared_ptr<SdpAnswer> answer);

    void setConnectionState(ConnectionState state) SRTC_LOCKS_EXCLUDED(mMutex, mListenerMutex);

    void startConnecting();

    bool mIsStarted SRTC_GUARDED_BY(mMutex) = { false };
    bool mIsQuit SRTC_GUARDED_BY(mMutex) = { false };
    std::thread mThread SRTC_GUARDED_BY(mMutex);

    int mEventHandle SRTC_GUARDED_BY(mMutex) = { -1 };

    struct FrameToSend {
        std::shared_ptr<Track> track;
        std::shared_ptr<Packetizer> packetizer;
        ByteBuffer buf;                 // possibly empty
        std::vector<ByteBuffer> csd;    // possibly empty
    };

    std::list<PeerCandidate::FrameToSend> mFrameSendQueue;

    // PeerCandidateListener
    void onCandidateHasDataToSend(PeerCandidate* candidate) override;
    void onCandidateConnecting(PeerCandidate* candidate) override;
    void onCandidateIceConnected(PeerCandidate* candidate) override;
    void onCandidateDtlsConnected(PeerCandidate* candidate) override;
    void onCandidateFailedToConnect(PeerCandidate* candidate, const Error& error) override;
    void onCandidateLostConnection(PeerCandidate* candidate, const Error& error) override;

    // Overall connection state and listener
    ConnectionState mConnectionState SRTC_GUARDED_BY(mMutex) = { ConnectionState::Inactive };

    std::mutex mListenerMutex;
    ConnectionStateListener mConnectionStateListener SRTC_GUARDED_BY(mListenerMutex);

    // Packetizers
    std::shared_ptr<Packetizer> mVideoSinglePacketizer SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<Packetizer> mAudioPacketizer SRTC_GUARDED_BY(mMutex);

    // These are only used on the worker thread so don't need mutexes
    std::shared_ptr<LoopScheduler> mLoopScheduler;
    std::shared_ptr<PeerCandidate> mSelectedCandidate;
    std::list<std::shared_ptr<PeerCandidate>> mConnectingCandidateList;
    int mEpollHandle SRTC_GUARDED_BY(mMutex) = { 0 };
};

}

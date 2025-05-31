#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/error.h"
#include "srtc/peer_candidate_listener.h"
#include "srtc/publish_config.h"
#include "srtc/scheduler.h"
#include "srtc/sdp_offer.h"
#include "srtc/srtc.h"
#include "srtc/track_selector.h"

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace srtc
{

class SdpAnswer;
class SdpOffer;
class Track;
class Packetizer;
class Scheduler;
class PeerCandidate;
class EventLoop;

class PeerConnection final : public PeerCandidateListener
{
public:
    PeerConnection();
    ~PeerConnection();

    std::shared_ptr<SdpOffer> createPublishSdpOffer(const OfferConfig& config,
                                                    const std::optional<PubVideoConfig>& videoConfig,
                                                    const std::optional<PubAudioConfig>& audioConfig);
    Error setSdpOffer(const std::shared_ptr<SdpOffer>& offer);

    std::pair<std::shared_ptr<SdpAnswer>, Error> parsePublishSdpAnswer(const std::shared_ptr<SdpOffer>& offer,
                                                                       const std::string& answer,
                                                                       const std::shared_ptr<TrackSelector>& selector);
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

    using PublishConnectionStatsListener = std::function<void(const PublishConnectionStats&)>;
    void setPublishConnectionStatsListener(const PublishConnectionStatsListener& listener);

    Error setVideoSingleCodecSpecificData(std::vector<ByteBuffer>&& list);
    Error publishVideoSingleFrame(ByteBuffer&& buf);

    Error setVideoSimulcastCodecSpecificData(const std::string& layerName, std::vector<ByteBuffer>&& list);
    Error publishVideoSimulcastFrame(const std::string& layerName, ByteBuffer&& buf);

    Error publishAudioFrame(ByteBuffer&& buf);

private:
    mutable std::mutex mMutex;

    std::shared_ptr<SdpOffer> mSdpOffer SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<SdpAnswer> mSdpAnswer SRTC_GUARDED_BY(mMutex);

    std::shared_ptr<Track> mVideoSingleTrack SRTC_GUARDED_BY(mMutex);
    std::vector<std::shared_ptr<Track>> mVideoSimulcastTrackList SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<Track> mAudioTrack SRTC_GUARDED_BY(mMutex);

    struct LayerInfo {
        LayerInfo(const std::string& ridName,
                  const std::shared_ptr<Track>& track,
                  const std::shared_ptr<Packetizer>& packetizer)
            : ridName(ridName)
            , track(track)
            , packetizer(packetizer)
        {
        }

        std::string ridName;
        std::shared_ptr<Track> track;
        std::shared_ptr<Packetizer> packetizer;
    };
    std::vector<LayerInfo> mVideoSimulcastLayerList;

    void networkThreadWorkerFunc(std::shared_ptr<SdpOffer> offer, std::shared_ptr<SdpAnswer> answer);

    void setConnectionState(ConnectionState state) SRTC_LOCKS_EXCLUDED(mMutex, mListenerMutex);

    void startConnecting();

    std::vector<std::shared_ptr<Track>> collectTracksLocked() const SRTC_SHARED_LOCKS_REQUIRED(mMutex);

    bool mIsStarted SRTC_GUARDED_BY(mMutex) = { false };
    bool mIsQuit SRTC_GUARDED_BY(mMutex) = { false };
    std::thread mThread SRTC_GUARDED_BY(mMutex);

    // Event loop
    const std::shared_ptr<EventLoop> mEventLoop SRTC_GUARDED_BY(mMutex);

    struct FrameToSend {
        std::shared_ptr<Track> track;
        std::shared_ptr<Packetizer> packetizer;
        ByteBuffer buf;              // possibly empty
        std::vector<ByteBuffer> csd; // possibly empty
    };

    std::list<FrameToSend> mFrameSendQueue;

    // PeerCandidateListener
    void onCandidateHasDataToSend(PeerCandidate* candidate) override;
    void onCandidateConnecting(PeerCandidate* candidate) override;
    void onCandidateIceConnected(PeerCandidate* candidate) override;
    void onCandidateDtlsConnected(PeerCandidate* candidate) override;
    void onCandidateFailedToConnect(PeerCandidate* candidate, const Error& error) override;

    // Overall connection state and listener
    ConnectionState mConnectionState SRTC_GUARDED_BY(mMutex) = { ConnectionState::Inactive };

    std::mutex mListenerMutex;
    ConnectionStateListener mConnectionStateListener SRTC_GUARDED_BY(mListenerMutex);
    PublishConnectionStatsListener mPublishConnectionStatsListener SRTC_GUARDED_BY(mListenerMutex);

    // Packetizers
    std::shared_ptr<Packetizer> mVideoSinglePacketizer SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<Packetizer> mAudioPacketizer SRTC_GUARDED_BY(mMutex);

    // Sender reports
    void sendSenderReports();
    std::weak_ptr<Task> mTaskSenderReports;

    // Connection stats
    void sendConnectionStats();
    std::weak_ptr<Task> mTaskConnectionStats;

    // These are only used on the worker thread so don't need mutexes
    std::shared_ptr<LoopScheduler> mLoopScheduler;
    std::shared_ptr<PeerCandidate> mSelectedCandidate;
    std::list<std::shared_ptr<PeerCandidate>> mConnectingCandidateList;
};

} // namespace srtc

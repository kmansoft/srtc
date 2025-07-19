#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/error.h"
#include "srtc/jitter_buffer.h"
#include "srtc/peer_candidate_listener.h"
#include "srtc/publish_config.h"
#include "srtc/random_generator.h"
#include "srtc/scheduler.h"
#include "srtc/sdp_offer.h"
#include "srtc/srtc.h"
#include "srtc/subscribe_config.h"
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
    explicit PeerConnection(Direction direction);
    ~PeerConnection() override;

    // SDP offer
    using OfferAndError = std::pair<std::shared_ptr<SdpOffer>, Error>;

    OfferAndError createPublishOffer(const PubOfferConfig& pubConfig,
                                     const std::optional<PubVideoConfig>& videoConfig,
                                     const std::optional<PubAudioConfig>& audioConfig);
    OfferAndError createSubscribeOffer(const SubOfferConfig& subConfig,
                                       const std::optional<SubVideoConfig>& videoConfig,
                                       const std::optional<SubAudioConfig>& audioConfig);
    Error setOffer(const std::shared_ptr<SdpOffer>& offer);

    // SDP answer
    using AnswerAndError = std::pair<std::shared_ptr<SdpAnswer>, Error>;

    AnswerAndError parsePublishAnswer(const std::shared_ptr<SdpOffer>& offer,
                                      const std::string& answer,
                                      const std::shared_ptr<TrackSelector>& selector);
    AnswerAndError parseSubscribeAnswer(const std::shared_ptr<SdpOffer>& offer,
                                        const std::string& answer,
                                        const std::shared_ptr<TrackSelector>& selector);
    Error setAnswer(const std::shared_ptr<SdpAnswer>& answer);

    std::shared_ptr<SdpOffer> getOffer() const;
    std::shared_ptr<SdpAnswer> getAnswer() const;

    std::shared_ptr<Track> getVideoSingleTrack() const;
    std::vector<std::shared_ptr<Track>> getVideoSimulcastTrackList() const;
    std::shared_ptr<Track> getAudioTrack() const;

    // Connection state listener
    enum class ConnectionState {
        Inactive = 0,
        Connecting = 1,
        Connected = 2,
        Failed = 100,
        Closed = 200
    };
    using ConnectionStateListener = std::function<void(ConnectionState state)>;
    void setConnectionStateListener(const ConnectionStateListener& listener);

    // Publish stats listener
    using PublishConnectionStatsListener = std::function<void(const PublishConnectionStats&)>;
    void setPublishConnectionStatsListener(const PublishConnectionStatsListener& listener);

    // Publishing media
    Error setVideoSingleCodecSpecificData(std::vector<ByteBuffer>&& list);
    Error publishVideoSingleFrame(ByteBuffer&& buf);

    Error setVideoSimulcastCodecSpecificData(const std::string& layerName, std::vector<ByteBuffer>&& list);
    Error publishVideoSimulcastFrame(const std::string& layerName, ByteBuffer&& buf);

    Error publishAudioFrame(ByteBuffer&& buf);

    // Subscribing
    using SubscribeEncodedFrameListener = std::function<void(const std::shared_ptr<EncodedFrame>&)>;
    void setSubscribeEncodedFrameListener(const SubscribeEncodedFrameListener& listener);

    using SubscribeSenderReportListener = std::function<void(const std::shared_ptr<Track>&, const SenderReport&)>;
    void setSubscribeSenderReportsListener(const SubscribeSenderReportListener& listener);

    // Closing
    void close();

private:
    const Direction mDirection;

    mutable std::mutex mMutex;

    std::shared_ptr<SdpOffer> mSdpOffer SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<SdpAnswer> mSdpAnswer SRTC_GUARDED_BY(mMutex);

    std::shared_ptr<Track> mVideoSingleTrack;
    std::vector<std::shared_ptr<Track>> mVideoSimulcastTrackList;
    std::shared_ptr<Track> mAudioTrack;

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

    void networkThreadWorkerFunc();

    void setConnectionState(ConnectionState state) SRTC_LOCKS_EXCLUDED(mMutex, mListenerMutex);

    void startConnecting();

    std::vector<std::shared_ptr<Track>> collectTracks() const;

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

    // Jitter buffer processing
    void processJitterBuffer(const std::shared_ptr<JitterBuffer>& buffer);

    // PeerCandidateListener
    void onCandidateHasDataToSend(PeerCandidate* candidate) override;
    void onCandidateConnecting(PeerCandidate* candidate) override;
    void onCandidateIceSelected(PeerCandidate* candidate) override;
    void onCandidateConnected(PeerCandidate* candidate) override;
    void onCandidateFailedToConnect(PeerCandidate* candidate, const Error& error) override;
    void onCandidateReceivedMediaPacket(PeerCandidate* candiate, const std::shared_ptr<RtpPacket>& packet) override;
    void onCandidateReceivedSenderReport(PeerCandidate* candidate,
                                         const std::shared_ptr<Track>& track,
                                         const SenderReport& sr) override;

    // Overall connection state and listener
    ConnectionState mConnectionState SRTC_GUARDED_BY(mMutex) = { ConnectionState::Inactive };

    std::mutex mListenerMutex;
    ConnectionStateListener mConnectionStateListener SRTC_GUARDED_BY(mListenerMutex);
    PublishConnectionStatsListener mPublishConnectionStatsListener SRTC_GUARDED_BY(mListenerMutex);
    SubscribeEncodedFrameListener mSubscribeEncodedFrameListener SRTC_GUARDED_BY(mListenerMutex);
    SubscribeSenderReportListener mSubscribeSenderReportsListener SRTC_GUARDED_BY(mListenerMutex);

    // Packetizers
    std::shared_ptr<Packetizer> mVideoSinglePacketizer;
    std::shared_ptr<Packetizer> mAudioPacketizer;

    // Jitter buffers
    std::shared_ptr<JitterBuffer> mJitterBufferVideo;
    std::shared_ptr<JitterBuffer> mJitterBufferAudio;

    // Sender and received reports
    void sendReports();
    std::weak_ptr<Task> mTaskReports;

    // Connection stats
    void sendConnectionStats();
    std::weak_ptr<Task> mTaskConnectionStats;

    // Subscribe PLI
    void sendPictureLossIndicator();
    std::weak_ptr<Task> mTaskPictureLossIndicator;

    // These are only used on the worker thread so don't need mutexes
    std::shared_ptr<LoopScheduler> mLoopScheduler;
    std::shared_ptr<PeerCandidate> mSelectedCandidate;
    std::list<std::shared_ptr<PeerCandidate>> mConnectingCandidateList;

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

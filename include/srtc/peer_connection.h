#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/error.h"
#include "srtc/jitter_buffer.h"
#include "srtc/peer_candidate_listener.h"
#include "srtc/publish_config.h"
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
struct DataChannelMessage;

class PeerConnection final : PeerCandidateListener
{
public:
    explicit PeerConnection(Direction direction);
    ~PeerConnection() override;

    // SDP offer
    using OfferAndError = std::pair<std::shared_ptr<SdpOffer>, Error>;

    OfferAndError createPublishOffer(const PubOfferConfig& pubConfig, const PubMediaConfig& mediaConfig);
    OfferAndError createSubscribeOffer(const SubOfferConfig& subConfig, const SubMediaConfig& mediaConfig);
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

    std::vector<std::shared_ptr<Media>> getMediaList() const;
    std::vector<std::shared_ptr<Track>> getTrackList() const;

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

    // Publish listeners
    using PublishConnectionStatsListener = std::function<void(const PublishConnectionStats&)>;
    void setPublishConnectionStatsListener(const PublishConnectionStatsListener& listener);

    using PublishKeyFrameRequestedListener = std::function<void()>;
    void setPublishKeyFrameRequestedListener(const PublishKeyFrameRequestedListener& listener);

    // Publishing media
    Error setVideoCodecSpecificData(const std::shared_ptr<Track>& track, std::vector<ByteBuffer>&& list);
    Error publishVideoFrame(const std::shared_ptr<Track>& track, int64_t pts_usec, ByteBuffer&& buf);
    Error updateVideoSimulcastLayer(const std::shared_ptr<Track>& track, const SimulcastLayer& layer);
    Error publishAudioFrame(const std::shared_ptr<Track>& track, int64_t pts_usec, ByteBuffer&& buf);

    // Subscribe listeners
    using SubscribeConnectionStatsListener = std::function<void(const SubscribeConnectionStats&)>;
    void setSubscribeConnectionStatsListener(const SubscribeConnectionStatsListener& listener);

    // Subscribing
    using SubscribeEncodedFrameListener = std::function<void(const std::shared_ptr<EncodedFrame>&)>;
    void setSubscribeEncodedFrameListener(const SubscribeEncodedFrameListener& listener);

    using SubscribeSenderReportListener = std::function<void(const std::shared_ptr<Track>&, const SenderReport&)>;
    void setSubscribeSenderReportsListener(const SubscribeSenderReportListener& listener);

    // Data channels
    [[nodiscard]] uint32_t getDataChannelMaxMessageSize() const;
    [[nodiscard]] Error sendDataChannelText(const std::string& label, std::string&& data);
    [[nodiscard]] Error sendDataChannelBinary(const std::string& label, ByteBuffer&& data);

    struct DataChannelListener {
        virtual ~DataChannelListener();

        virtual void onDataChannelOpened(const std::string& label) = 0;
        virtual void onDataChannelClosed(const std::string& label) = 0;

        virtual void onDataChannelReceivedText(const std::string& label, const std::string& data) = 0;
        virtual void onDataChannelReceivedBinary(const std::string& label, const ByteBuffer& data) = 0;
    };
    void setDataChannelListener(const std::shared_ptr<DataChannelListener>& listener);

    // Closing
    void close();

private:
    const Direction mDirection;

    mutable std::mutex mMutex;

    std::shared_ptr<SdpOffer> mSdpOffer SRTC_GUARDED_BY(mMutex);
    std::shared_ptr<SdpAnswer> mSdpAnswer SRTC_GUARDED_BY(mMutex);
    bool mDataChannelsNegotiated = false;
    uint32_t mDataChannelMaxMessageSize = 0;

    void networkThreadWorkerFunc();

    void setConnectionState(ConnectionState state) SRTC_LOCKS_EXCLUDED(mMutex, mListenerMutex);

    void startConnecting();

    std::vector<std::shared_ptr<Track>> collectTracks() const;

    bool mIsStarted SRTC_GUARDED_BY(mMutex) = { false };
    bool mIsQuit SRTC_GUARDED_BY(mMutex) = { false };
    std::thread mThread SRTC_GUARDED_BY(mMutex);

    // Event loop
    const std::shared_ptr<EventLoop> mEventLoop;

    struct FrameToSend {
        int64_t pts_usec;
        std::shared_ptr<Track> track;
        std::shared_ptr<Packetizer> packetizer;
        ByteBuffer buf;                      // possibly empty
        std::vector<ByteBuffer> csd;         // possibly empty
        std::optional<SimulcastLayer> layer; // possibly empty
    };

    std::list<FrameToSend> mFrameSendQueue SRTC_GUARDED_BY(mMutex);
    std::list<DataChannelMessage> mDataSendQueue SRTC_GUARDED_BY(mMutex);

    // Jitter buffer processing
    void processJitterBuffer(const std::shared_ptr<JitterBuffer>& buffer);

    // PeerCandidateListener
    void onCandidateHasDataToSend(PeerCandidate* candidate) override;
    void onCandidateConnecting(PeerCandidate* candidate) override;
    void onCandidateIceConnected(PeerCandidate* candidate) override;
    void onCandidateDtlsConnected(PeerCandidate* candidate) override;
    void onCandidateDtlsDisconnected(PeerCandidate* candidate, const Error& error) override;
    void onCandidateFailedToConnect(PeerCandidate* candidate, const Error& error) override;
    void onCandidateReceivedMediaPacket(PeerCandidate* candiate, const std::shared_ptr<RtpPacket>& packet) override;
    void onCandidateReceivedSenderReport(PeerCandidate* candidate,
                                         const std::shared_ptr<Track>& track,
                                         const SenderReport& sr) override;
    void onCandidateReceivedKeyFrameRequest(PeerCandidate* candiate) override;
    void getSimulcastLayerList(const std::shared_ptr<Media>& media,
                               std::vector<SimulcastLayer>& layerList) const override;

    void onSctpDataChannelOpen(const std::string& label) override;
    void onSctpDataChannelText(const std::string& label, const std::string& data) override;
    void onSctpDataChannelBinary(const std::string& label, const ByteBuffer& data) override;
    void onSctpDataChannelClosed(const std::string& label) override;

    // Overall connection state and listener
    ConnectionState mConnectionState SRTC_GUARDED_BY(mMutex);

    std::mutex mListenerMutex;
    ConnectionStateListener mConnectionStateListener SRTC_GUARDED_BY(mListenerMutex);
    PublishConnectionStatsListener mPublishConnectionStatsListener SRTC_GUARDED_BY(mListenerMutex);
    PublishKeyFrameRequestedListener mPublishKeyFrameRequestedListener SRTC_GUARDED_BY(mListenerMutex);
    SubscribeConnectionStatsListener mSubscribeConnectionStatsListener SRTC_GUARDED_BY(mListenerMutex);
    SubscribeEncodedFrameListener mSubscribeEncodedFrameListener SRTC_GUARDED_BY(mListenerMutex);
    SubscribeSenderReportListener mSubscribeSenderReportsListener SRTC_GUARDED_BY(mListenerMutex);
    std::shared_ptr<DataChannelListener> mDataChannelListener SRTC_GUARDED_BY(mListenerMutex);

    // Media
    struct MediaEntry {
        const std::shared_ptr<Media> media;
        std::vector<SimulcastLayer> layerList;

        MediaEntry(const std::shared_ptr<Media>& media)
            : media(media)
        {
        }
    };
    std::vector<MediaEntry> mMediaEntryList;

    // Tracks
    struct TrackEntry {
        const std::shared_ptr<Track> track;

        // Publishing
        std::shared_ptr<Packetizer> packetizer;

        // Subscribing
        std::shared_ptr<Depacketizer> depacketizer;
        std::shared_ptr<JitterBuffer> jitterBuffer;

        TrackEntry(const std::shared_ptr<Track>& track)
            : track(track)
        {
        }
    };
    std::vector<TrackEntry> mTrackEntryList;

    // Sender and receiver reports
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
};

} // namespace srtc

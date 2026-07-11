#include "srtc/peer_connection.h"
#include "srtc/data_channel_message.h"
#include "srtc/depacketizer.h"
#include "srtc/event_loop.h"
#include "srtc/ice_agent.h"
#include "srtc/jitter_buffer.h"
#include "srtc/logging.h"
#include "srtc/media.h"
#include "srtc/packetizer.h"
#include "srtc/peer_candidate.h"
#include "srtc/rtcp_packet_source.h"
#include "srtc/sdp_answer.h"
#include "srtc/srtc.h"
#include "srtc/srtp_connection.h"
#include "srtc/track.h"
#include "srtc/track_stats.h"
#include "stunmessage.h"

#include <algorithm>
#include <cassert>

#define LOG(level, ...) srtc::log(level, "PeerConnection", __VA_ARGS__)

namespace
{

std::once_flag gInitFlag;

constexpr auto kReportsInterval = std::chrono::seconds(1);
constexpr auto kConnectionStatsInterval = std::chrono::seconds(5);
constexpr auto kJitterBufferSize = 4096;

template <typename T>
srtc::Error validateMediaItem(const T& mediaItem)
{
    if (mediaItem.media_id.empty()) {
        return { srtc::Error::Code::InvalidData, "The media id cannot be empty" };
    }

    for (const auto& codec : mediaItem.codec_list) {
        if (mediaItem.media_type == srtc::MediaType::Audio) {
            if (!isAudioCodec(codec.codec)) {
                return { srtc::Error::Code::InvalidData, "An audio media can only have audio codecs" };
            }
        } else if (mediaItem.media_type == srtc::MediaType::Video) {
            if (!isVideoCodec(codec.codec)) {
                return { srtc::Error::Code::InvalidData, "A video media can only have video codecs" };
            }
        }
    }

    return srtc::Error::OK;
}

} // namespace

namespace srtc
{

PeerConnection::PeerConnection(Direction direction)
    : mDirection(direction)
    , mEventLoop(EventLoop::factory())
    , mConnectionState(ConnectionState::Inactive)
{
    std::call_once(gInitFlag, [] {
        // Just in case we need something
    });
}

PeerConnection::~PeerConnection()
{
    close();
}

std::pair<std::shared_ptr<SdpOffer>, Error> PeerConnection::createPublishOffer(const PubOfferConfig& pubConfig,
                                                                               const PubMediaConfig& mediaConfig)
{
    if (mDirection != Direction::Publish) {
        return { {}, { Error::Code::InvalidData, "The peer connection's direction is not publish" } };
    }

    SdpOffer::Config config;
    config.cname = pubConfig.cname;
    config.data_channels = pubConfig.data_channel_config.data_channels;
    config.enable_rtx = pubConfig.enable_rtx;
    config.enable_bwe = pubConfig.enable_bwe;
    config.enable_rfc8851 = pubConfig.enable_rfc8851;

    std::vector<SdpOffer::MediaLine> media;

    for (const auto& pubMediaItem : mediaConfig.media_list) {
        media.emplace_back();
        auto& mediaLine = media.back();

        if (const auto error = validateMediaItem(pubMediaItem); error.isError()) {
            return { {}, error };
        }

        mediaLine.id = pubMediaItem.media_id;
        mediaLine.mediaType = pubMediaItem.media_type;

        for (const auto& codec : pubMediaItem.codec_list) {
            mediaLine.codec_list.emplace_back(codec.codec, codec.profile_level_id, codec.minptime, codec.stereo);
        }

        for (const auto& layer : pubMediaItem.layer_list) {
            mediaLine.layer_list.emplace_back();
            auto& outLayer = mediaLine.layer_list.back();
            outLayer.name = layer.name;
            outLayer.width = layer.width;
            outLayer.height = layer.height;
            outLayer.frames_per_second = layer.frames_per_second;
            outLayer.kilobits_per_second = layer.kilobits_per_second;
        }
    }

    return { std::shared_ptr<SdpOffer>(new SdpOffer(Direction::Publish, config, media)), Error::OK };
}

std::pair<std::shared_ptr<SdpOffer>, Error> PeerConnection::createSubscribeOffer(const SubOfferConfig& subConfig,
                                                                                 const SubMediaConfig& mediaConfig)
{
    if (mDirection != Direction::Subscribe) {
        return { {}, { Error::Code::InvalidData, "The peer connection's direction is not subscribe" } };
    }

    SdpOffer::Config config;
    config.cname = subConfig.cname;
    config.data_channels = subConfig.data_channel_config.data_channels;
    config.pli_interval_millis = subConfig.pli_interval_millis;
    config.jitter_buffer_length_millis = subConfig.jitter_buffer_length_millis;
    config.jitter_buffer_nack_delay_millis = subConfig.jitter_buffer_nack_delay_millis;

    std::vector<SdpOffer::MediaLine> media;

    for (const auto& pubMediaItem : mediaConfig.media_list) {
        media.emplace_back();
        auto& mediaLine = media.back();

        if (const auto error = validateMediaItem(pubMediaItem); error.isError()) {
            return { {}, error };
        }

        mediaLine.id = pubMediaItem.media_id;
        mediaLine.mediaType = pubMediaItem.media_type;

        for (const auto& codec : pubMediaItem.codec_list) {
            mediaLine.codec_list.emplace_back(codec.codec, codec.profile_level_id, codec.minptime, codec.stereo);
        }
    }

    return { std::shared_ptr<SdpOffer>(new SdpOffer(Direction::Subscribe, config, media)), Error::OK };
}

Error PeerConnection::setOffer(const std::shared_ptr<SdpOffer>& offer)
{
    if (mDirection != offer->getDirection()) {
        return { Error::Code::InvalidData, "Wrong publish / subscribe direction for the offer" };
    }

    std::lock_guard lock(mMutex);

    assert(!mIsQuit);

    if (mSdpOffer) {
        return { Error::Code::InvalidData, "Connection already has an SDP offer" };
    }
    if (mIsStarted) {
        return { Error::Code::InvalidData, "Connection is already started" };
    }

    mSdpOffer = offer;

    return Error::OK;
}

std::pair<std::shared_ptr<SdpAnswer>, Error> PeerConnection::parsePublishAnswer(
    const std::shared_ptr<SdpOffer>& offer, const std::string& answer, const std::shared_ptr<TrackSelector>& selector)
{
    if (mDirection != Direction::Publish) {
        return { {}, { Error::Code::InvalidData, "The peer connection's direction is not publish" } };
    }

    return SdpAnswer::parse(offer, answer, selector);
}

std::pair<std::shared_ptr<SdpAnswer>, Error> PeerConnection::parseSubscribeAnswer(
    const std::shared_ptr<SdpOffer>& offer, const std::string& answer, const std::shared_ptr<TrackSelector>& selector)
{
    if (mDirection != Direction::Subscribe) {
        return { {}, { Error::Code::InvalidData, "The peer connection's direction is not subscribe" } };
    }

    return SdpAnswer::parse(offer, answer, selector);
}

Error PeerConnection::setAnswer(const std::shared_ptr<SdpAnswer>& answer)
{
    std::lock_guard lock(mMutex);

    assert(!mIsQuit);

    if (mIsStarted) {
        return { Error::Code::InvalidData, "Connection is already started" };
    }
    if (mSdpAnswer) {
        return { Error::Code::InvalidData, "Connection already has an SDP answer" };
    }

    mSdpAnswer = answer;

    if (mSdpOffer && mSdpAnswer) {
        if (mSdpOffer->getDirection() != mSdpAnswer->getDirection()) {
            return { Error::Code::InvalidData, "Offer and answer must use same direction, publish or subscribe" };
        }

        for (const auto& media : answer->getMediaList()) {
            mMediaEntryList.emplace_back(media);
        }
        for (const auto& track : answer->getTrackList()) {
            mTrackEntryList.emplace_back(track);
        }

        mDataChannelsNegotiated = mSdpOffer->hasDataChannel() && mSdpAnswer->hasDataChannel();
        if (mDataChannelsNegotiated) {
            mDataChannelMaxMessageSize = std::min(mSdpOffer->getSctpMaxMessageSize(), mSdpAnswer->getMaxMessageSize());
        }

        if (mDirection == Direction::Publish) {
            // Packetizers
            for (auto& entry : mTrackEntryList) {
                const auto [packetizer, error] = Packetizer::make(entry.track);
                if (error.isError()) {
                    return error;
                }
                entry.packetizer = packetizer;
            }

            // Simulcast layers
            for (auto& mediaEntry : mMediaEntryList) {
                if (mediaEntry.media->getType() == MediaType::Video) {
                    for (const auto& trackEntry : mTrackEntryList) {
                        if (trackEntry.track->getMedia() == mediaEntry.media && trackEntry.track->isSimulcast()) {
                            mediaEntry.layerList.push_back(*trackEntry.track->getSimulcastLayer());
                        }
                    }
                }
            }
        } else if (mDirection == Direction::Subscribe) {
            // Jitter buffers are created when we connect because we have to know the rtt
            for (auto& entry : mTrackEntryList) {
                const auto [depacketizer, error] = Depacketizer::make(entry.track);
                if (error.isError()) {
                    return error;
                }
                entry.depacketizer = depacketizer;
            }
        }

        // We are started
        mIsStarted = true;

        // The network thread
        mThread = std::thread(&PeerConnection::networkThreadWorkerFunc, this);
    }

    return Error::OK;
}

std::shared_ptr<SdpOffer> PeerConnection::getOffer() const
{
    std::lock_guard lock(mMutex);
    return mSdpOffer;
}

std::shared_ptr<SdpAnswer> PeerConnection::getAnswer() const
{
    std::lock_guard lock(mMutex);
    return mSdpAnswer;
}

std::vector<std::shared_ptr<Media>> PeerConnection::getMediaList() const
{
    std::vector<std::shared_ptr<Media>> list;

    std::lock_guard lock(mMutex);

    for (const auto& entry : mMediaEntryList) {
        list.push_back(entry.media);
    }

    return list;
}

std::vector<std::shared_ptr<Track>> PeerConnection::getTrackList() const
{
    std::vector<std::shared_ptr<Track>> list;

    std::lock_guard lock(mMutex);

    for (const auto& entry : mTrackEntryList) {
        list.push_back(entry.track);
    }

    return list;
}

void PeerConnection::setConnectionStateListener(const ConnectionStateListener& listener)
{
    std::lock_guard lock(mListenerMutex);
    mConnectionStateListener = listener;
}

void PeerConnection::setPublishConnectionStatsListener(const PublishConnectionStatsListener& listener)
{
    std::lock_guard lock(mListenerMutex);
    mPublishConnectionStatsListener = listener;
}

void PeerConnection::setPublishKeyFrameRequestedListener(const PublishKeyFrameRequestedListener& listener)
{
    std::lock_guard lock(mListenerMutex);
    mPublishKeyFrameRequestedListener = listener;
}

Error PeerConnection::setVideoCodecSpecificData(const std::shared_ptr<Track>& track, std::vector<ByteBuffer>&& list)
{
    if (mDirection != Direction::Publish) {
        return { Error::Code::InvalidData, "The peer connection's direction is not publish" };
    }

    std::lock_guard lock(mMutex);

    if (mConnectionState != ConnectionState::Connected) {
        return Error::OK;
    }

    for (const auto& entry : mTrackEntryList) {
        if (entry.track == track) {
            FrameToSend fr = {};
            fr.track = track;
            fr.packetizer = entry.packetizer;
            fr.csd = std::move(list);

            mFrameSendQueue.push_back(std::move(fr));
            mEventLoop->interrupt();

            return Error::OK;
        }
    }

    return { Error::Code::InvalidData, "The track is not found" };
}

Error PeerConnection::publishVideoFrame(const std::shared_ptr<Track>& track, int64_t pts_usec, ByteBuffer&& buf)
{
    if (mDirection != Direction::Publish) {
        return { Error::Code::InvalidData, "The peer connection's direction is not publish" };
    }

    std::lock_guard lock(mMutex);

    if (mConnectionState != ConnectionState::Connected) {
        return Error::OK;
    }

    for (const auto& entry : mTrackEntryList) {
        if (entry.track == track) {
            FrameToSend fr = {};
            fr.pts_usec = pts_usec;
            fr.track = track;
            fr.packetizer = entry.packetizer;
            fr.buf = std::move(buf);

            mFrameSendQueue.push_back(std::move(fr));
            mEventLoop->interrupt();

            return Error::OK;
        }
    }

    return { Error::Code::InvalidData, "The track is not found" };
}

Error PeerConnection::updateVideoSimulcastLayer(const std::shared_ptr<Track>& track, const SimulcastLayer& updated)
{
    if (mDirection != Direction::Publish) {
        return { Error::Code::InvalidData, "The peer connection's direction is not publish" };
    }

    const auto media = track->getMedia();
    if (media->getType() != MediaType::Video) {
        return { Error::Code::InvalidData, "The track media type is not video" };
    }

    if (!track->isSimulcast()) {
        return { Error::Code::InvalidData, "The track is not simulcast" };
    }

    const auto layer = track->getSimulcastLayer();
    if (layer->name != updated.name) {
        return { Error::Code::InvalidData, "The track name does not match the layer name" };
    }

    std::lock_guard lock(mMutex);

    if (mConnectionState != ConnectionState::Connected) {
        return Error::OK;
    }

    for (const auto& entry : mTrackEntryList) {
        if (entry.track == track) {
            FrameToSend fr = {};
            fr.track = track;
            fr.packetizer = entry.packetizer;
            fr.layer = updated;

            mFrameSendQueue.push_back(std::move(fr));
            mEventLoop->interrupt();

            return Error::OK;
        }
    }

    return { Error::Code::InvalidData, "The track is not found" };
}

Error PeerConnection::publishAudioFrame(const std::shared_ptr<Track>& track, int64_t pts_usec, ByteBuffer&& buf)
{
    if (mDirection != Direction::Publish) {
        return { Error::Code::InvalidData, "The peer connection's direction is not publish" };
    }

    std::lock_guard lock(mMutex);

    if (mConnectionState != ConnectionState::Connected) {
        return Error::OK;
    }

    for (const auto& entry : mTrackEntryList) {
        if (entry.track == track) {
            FrameToSend fr = {};
            fr.pts_usec = pts_usec;
            fr.track = track;
            fr.packetizer = entry.packetizer;
            fr.buf = std::move(buf);

            mFrameSendQueue.push_back(std::move(fr));
            mEventLoop->interrupt();

            return Error::OK;
        }
    }

    return { Error::Code::InvalidData, "The track is not found" };
}

void PeerConnection::setSubscribeConnectionStatsListener(const SubscribeConnectionStatsListener& listener)
{
    std::lock_guard lock(mListenerMutex);
    mSubscribeConnectionStatsListener = listener;
}

void PeerConnection::setSubscribeEncodedFrameListener(const SubscribeEncodedFrameListener& listener)
{
    std::lock_guard lock(mListenerMutex);
    mSubscribeEncodedFrameListener = listener;
}

void PeerConnection::setSubscribeSenderReportsListener(const SubscribeSenderReportListener& listener)
{
    std::lock_guard lock(mListenerMutex);
    mSubscribeSenderReportsListener = listener;
}

uint32_t PeerConnection::getDataChannelMaxMessageSize() const
{
    return mDataChannelMaxMessageSize;
}

Error PeerConnection::sendDataChannelText(const std::string& label, std::string&& data)
{
    std::lock_guard lock(mMutex);

    if (!mDataChannelsNegotiated) {
        return { Error::Code::InvalidData, "Data channels were not negotiated" };
    }
    if (mDataChannelMaxMessageSize > 0 && data.size() > mDataChannelMaxMessageSize) {
        return { Error::Code::InvalidData, "Message size exceeds peer maximum" };
    }

    mDataSendQueue.emplace_back(DataChannelMessage::makeText(label, std::move(data)));
    mEventLoop->interrupt();

    return Error::OK;
}

Error PeerConnection::sendDataChannelBinary(const std::string& label, ByteBuffer&& data)
{
    std::lock_guard lock(mMutex);

    if (!mDataChannelsNegotiated) {
        return { Error::Code::InvalidData, "Data channels were not negotiated" };
    }
    if (mDataChannelMaxMessageSize > 0 && data.size() > mDataChannelMaxMessageSize) {
        return { Error::Code::InvalidData, "Message size exceeds peer maximum" };
    }

    mDataSendQueue.emplace_back(DataChannelMessage::makeBinary(label, std::move(data)));
    mEventLoop->interrupt();

    return Error::OK;
}

PeerConnection::DataChannelListener::~DataChannelListener() = default;

void PeerConnection::setDataChannelListener(const std::shared_ptr<DataChannelListener>& listener)
{
    std::lock_guard lock(mListenerMutex);
    mDataChannelListener = listener;
}

void PeerConnection::close()
{
    std::thread waitForThread;

    {
        std::lock_guard lock(mMutex);

        if (mIsStarted) {
            mIsQuit = true;
            mEventLoop->interrupt();
            waitForThread = std::move(mThread);
        }
    }

    if (waitForThread.joinable()) {
        waitForThread.join();
    }
}

void PeerConnection::networkThreadWorkerFunc()
{
    // Loop scheduler
    mLoopScheduler = std::make_shared<LoopScheduler>();

    // We are connecting
    startConnecting();

    // Our processing loop
    while (true) {
        // Epoll for incoming data
        constexpr auto kDefaultTimeoutMillis = 100;

        auto timeout = mLoopScheduler->getTimeoutMillis(kDefaultTimeoutMillis);
        if (mSelectedCandidate) {
            if (const auto selectedTimeout = mSelectedCandidate->getTimeoutMillis(kDefaultTimeoutMillis);
                timeout > selectedTimeout) {
                timeout = selectedTimeout;
            }
        }

        for (const auto& trackEntry : mTrackEntryList) {
            if (trackEntry.jitterBuffer) {
                if (const auto jitterTimeout = trackEntry.jitterBuffer->getTimeoutMillis(kDefaultTimeoutMillis);
                    timeout > jitterTimeout) {
                    timeout = jitterTimeout;
                }
            }
        }

        std::vector<void*> udataList;
        mEventLoop->wait(udataList, timeout);

        std::list<FrameToSend> frameSendQueue;
        std::list<DataChannelMessage> dataSendQueue;

        {
            std::lock_guard lock(mMutex);
            if (mIsQuit) {
                break;
            }
            if (mConnectionState == ConnectionState::Failed || mConnectionState == ConnectionState::Closed) {
                break;
            }

            frameSendQueue = std::move(mFrameSendQueue);

            if (mSelectedCandidate) {
                dataSendQueue = std::move(mDataSendQueue);
            }
        }

        // Read data from the network
        for (const auto udata : udataList) {
            if (udata) {
                // Read from socket
                const auto ptr = static_cast<PeerCandidate*>(udata);
                ptr->receiveFromSocket();
            }
        }

        // Scheduler
        mLoopScheduler->run();

        for (auto& item : frameSendQueue) {
            // Update simulcast layer
            if (item.layer.has_value()) {
                const auto updated = item.layer.value();
                const auto media = item.track->getMedia();

                for (auto& mediaEntry : mMediaEntryList) {
                    if (mediaEntry.media == media) {
                        for (auto& layer : mediaEntry.layerList) {
                            if (layer.name == updated.name) {
                                layer.width = updated.width;
                                layer.height = updated.height;
                                layer.frames_per_second = updated.frames_per_second;
                                layer.kilobits_per_second = updated.kilobits_per_second;
                                break;
                            }
                        }
                    }
                }
            }

            // Frames to send
            if (mSelectedCandidate && (!item.buf.empty() || !item.csd.empty())) {
                mSelectedCandidate->addSendFrame(PeerCandidate::FrameToSend{
                    item.pts_usec, item.track, item.packetizer, std::move(item.buf), std::move(item.csd) });
            }
        }

        if (mSelectedCandidate) {
            for (auto& item : dataSendQueue) {
                mSelectedCandidate->sendDataChannelMessage(std::move(item));
            }
        }

        // Candidate processing
        for (const auto& candidate : mConnectingCandidateList) {
            candidate->run();
            if (mConnectingCandidateList.empty()) {
                // A candidate reached ICE connected, and we removed all connecting ones
                break;
            }
        }

        if (mSelectedCandidate) {
            mSelectedCandidate->run();
        }

        // Jitter buffer processing
        for (const auto& trackEntry : mTrackEntryList) {
            if (trackEntry.jitterBuffer) {
                processJitterBuffer(trackEntry.jitterBuffer);
            }
        }
    }

    mLoopScheduler.reset();

    // Clear everything on this thread before exiting
    mConnectingCandidateList.clear();
    mSelectedCandidate.reset();

    mMediaEntryList.clear();
    mTrackEntryList.clear();

    // We are all done
    setConnectionState(ConnectionState::Closed);
}

void PeerConnection::setConnectionState(ConnectionState state)
{
    {
        std::lock_guard lock1(mMutex);

        if (mConnectionState == state) {
            // Already the same
            return;
        }

        if (mConnectionState == ConnectionState::Failed || mConnectionState == ConnectionState::Closed) {
            // There is no escape
            return;
        }

        mConnectionState = state;

        if (mConnectionState == ConnectionState::Failed) {
            mIsQuit = true;
            mEventLoop->interrupt();
        }
    }

    {
        std::lock_guard lock2(mListenerMutex);
        if (mConnectionStateListener) {
            mConnectionStateListener(state);
        }
    }
}

void PeerConnection::startConnecting()
{
    setConnectionState(ConnectionState::Connecting);

    std::lock_guard lock(mMutex);

    mFrameSendQueue.clear();

    // We will need the track list
    const auto trackList = collectTracks();

    // Interleave IPv4 and IPv6 candidates
    std::vector<Host> hostList4;
    std::vector<Host> hostList6;
    for (const auto& host : mSdpAnswer->getHostList()) {
        if (host.addr.ss.ss_family == AF_INET) {
            hostList4.push_back(host);
        }
        if (host.addr.ss.ss_family == AF_INET6) {
            hostList6.push_back(host);
        }
    }

    const auto maxHostSize = std::max(hostList4.size(), hostList6.size());
    auto connectDelay = 0;
    for (size_t i = 0; i < maxHostSize; i += 1) {
        if (i < hostList4.size()) {
            const auto listener = static_cast<PeerCandidateListener*>(this);
            const auto candidate = std::make_shared<PeerCandidate>(listener,
                                                                   trackList,
                                                                   mSdpOffer,
                                                                   mSdpAnswer,
                                                                   mDataChannelMaxMessageSize,
                                                                   mLoopScheduler,
                                                                   hostList4[i],
                                                                   mEventLoop,
                                                                   std::chrono::milliseconds(connectDelay));
            mConnectingCandidateList.push_back(candidate);
        }
        if (i < hostList6.size()) {
            const auto listener = static_cast<PeerCandidateListener*>(this);
            const auto candidate = std::make_shared<PeerCandidate>(listener,
                                                                   trackList,
                                                                   mSdpOffer,
                                                                   mSdpAnswer,
                                                                   mDataChannelMaxMessageSize,
                                                                   mLoopScheduler,
                                                                   hostList6[i],
                                                                   mEventLoop,
                                                                   std::chrono::milliseconds(connectDelay));
            mConnectingCandidateList.push_back(candidate);
        }

        connectDelay += 100;
    }
}

void PeerConnection::processJitterBuffer(const std::shared_ptr<JitterBuffer>& buffer)
{
    const auto track = buffer->getTrack();
    const auto stats = track->getStats();

    const auto nackList = buffer->processNack();
    if (!nackList.empty()) {
        if (mSelectedCandidate) {
            stats->incrementReceivedPacketsLost(nackList.size());
            mSelectedCandidate->sendNacks(track, nackList);
        }
    }

    const auto frameList = buffer->processDeque();
    if (!frameList.empty()) {
        stats->incrementReceivedFrames(frameList.size());

        std::lock_guard lock(mListenerMutex);
        if (mSubscribeEncodedFrameListener) {
            for (const auto& frame : frameList) {
                mSubscribeEncodedFrameListener(frame);
            }
        }
    }
}

std::vector<std::shared_ptr<Track>> PeerConnection::collectTracks() const
{
    std::vector<std::shared_ptr<Track>> list;

    for (const auto& trackEntry : mTrackEntryList) {
        list.push_back(trackEntry.track);
    }

    return list;
}

void PeerConnection::onCandidateHasDataToSend([[maybe_unused]] PeerCandidate* candidate)
{
    std::lock_guard lock(mMutex);
    mEventLoop->interrupt();
}

void PeerConnection::onCandidateConnecting([[maybe_unused]] PeerCandidate* candidate)
{
    setConnectionState(ConnectionState::Connecting);

    for (auto& trackEntry : mTrackEntryList) {
        if (trackEntry.jitterBuffer) {
            trackEntry.jitterBuffer.reset();
        }
    }
}

void PeerConnection::onCandidateIceConnected(PeerCandidate* candidate)
{
    for (const auto& item : mConnectingCandidateList) {
        if (item.get() == candidate) {
            mSelectedCandidate = item;
            break;
        }
    }

    mConnectingCandidateList.clear();

    mLoopScheduler->dump();

    for (const auto& trackEntry : mTrackEntryList) {
        const auto trackItem = trackEntry.track;
        trackItem->getStats()->clear();

        trackItem->getRtpPacketSource()->clear();
        trackItem->getRtxPacketSource()->clear();
        trackItem->getRtcpPacketSource()->clear();
    }

    Task::cancelHelper(mTaskReports);
    mTaskReports = mLoopScheduler->submit(kReportsInterval, __FILE__, __LINE__, [this] { sendReports(); });

    Task::cancelHelper(mTaskConnectionStats);
    mTaskConnectionStats =
        mLoopScheduler->submit(kConnectionStatsInterval, __FILE__, __LINE__, [this] { sendConnectionStats(); });
}

void PeerConnection::onCandidateDtlsConnected(PeerCandidate* candidate)
{
    setConnectionState(ConnectionState::Connected);

    if (mDirection == Direction::Subscribe) {
        // We should have the rtt from ice
        const auto rtt = candidate->getIceRtt();
        assert(rtt.has_value());

        LOG(SRTC_LOG_V, "Creating jitter buffers, rtt = %.2f ms", rtt.value());

        std::lock_guard lock(mMutex);

        auto length = std::chrono::milliseconds(lround(rtt.value()) + 12);
        auto nackDelay = std::chrono::milliseconds(6);

        if (rtt.value() >= 50.0f) {
            length = std::chrono::milliseconds(lround(rtt.value()) + 25);
            nackDelay = std::chrono::milliseconds(10);
        }

#ifdef WIN32
        // Windows events are very low resolution, a wait can take 3-4-5 milliseconds longer (or more) than we ask
        // for. This can affect our nacks, we send them too late and they don't arrive in time.
        if (length.count() < 75) {
            length = std::chrono::milliseconds(75);
            nackDelay = std::chrono::milliseconds(10);
        }
#endif

        const auto config = mSdpOffer->getConfig();
        if (config.jitter_buffer_length_millis > length.count()) {
            length = std::chrono::milliseconds(config.jitter_buffer_length_millis);

            if (config.jitter_buffer_nack_delay_millis > nackDelay.count()) {
                nackDelay = std::chrono::milliseconds(config.jitter_buffer_nack_delay_millis);
            }
        }

        for (auto& trackEntry : mTrackEntryList) {
            trackEntry.depacketizer->reset();
            trackEntry.jitterBuffer = std::make_shared<JitterBuffer>(
                trackEntry.track, trackEntry.depacketizer, kJitterBufferSize, length, nackDelay);
        }
    }

    sendPictureLossIndicator();
}

void PeerConnection::onCandidateDtlsDisconnected(PeerCandidate* candidate, const Error& error)
{
    if (mSelectedCandidate.get() == candidate) {
        if (error.isOk()) {
            setConnectionState(ConnectionState::Closed);
        } else {
            LOG(SRTC_LOG_E, "DTLS disconnected with error: %d %s", static_cast<int>(error.code), error.message.c_str());
            setConnectionState(ConnectionState::Failed);
        }
    }
}

void PeerConnection::onCandidateFailedToConnect(PeerCandidate* candidate, const Error& error)
{
    LOG(SRTC_LOG_E, "Candidate failed to connect: %d %s", static_cast<int>(error.code), error.message.c_str());

    // We are connecting
    for (auto iter = mConnectingCandidateList.begin(); iter != mConnectingCandidateList.end();) {
        if (iter->get() == candidate) {
            iter = mConnectingCandidateList.erase(iter);
        } else {
            ++iter;
        }
    }

    // We have tried all candidates and they all failed
    if (mConnectingCandidateList.empty()) {
        setConnectionState(ConnectionState::Failed);
    }
}

void PeerConnection::onCandidateReceivedMediaPacket([[maybe_unused]] PeerCandidate* candiate,
                                                    const std::shared_ptr<RtpPacket>& packet)
{
    if (mDirection == Direction::Subscribe) {
        const auto track = packet->getTrack();

        const auto stats = track->getStats();
        stats->setHighestReceivedSeq(packet->getSequence());

        for (const auto& trackEntry : mTrackEntryList) {
            if (trackEntry.track == track) {
                trackEntry.jitterBuffer->consume(packet);
            }
        }
    }
}

void PeerConnection::onCandidateReceivedSenderReport([[maybe_unused]] PeerCandidate* candidate,
                                                     const std::shared_ptr<Track>& track,
                                                     const SenderReport& sr)
{
    if (mDirection == Direction::Subscribe) {
        std::lock_guard lock(mListenerMutex);
        if (mSubscribeSenderReportsListener) {
            mSubscribeSenderReportsListener(track, sr);
        }
    }
}

void PeerConnection::onCandidateReceivedKeyFrameRequest([[maybe_unused]] PeerCandidate* candiate)
{
    std::lock_guard lock(mListenerMutex);
    if (mPublishKeyFrameRequestedListener) {
        mPublishKeyFrameRequestedListener();
    }
}

void PeerConnection::getSimulcastLayerList(const std::shared_ptr<Media>& media,
                                           std::vector<SimulcastLayer>& layerList) const
{
    layerList.clear();

    for (const auto& mediaEntry : mMediaEntryList) {
        if (mediaEntry.media == media) {
            layerList.insert(layerList.end(), mediaEntry.layerList.begin(), mediaEntry.layerList.end());
            break;
        }
    }
}

void PeerConnection::onSctpDataChannelOpen(const std::string& label)
{
    std::lock_guard lock(mListenerMutex);
    if (mDataChannelListener) {
        mDataChannelListener->onDataChannelOpened(label);
    }
}

void PeerConnection::onSctpDataChannelText(const std::string& label, const std::string& data)
{
    std::lock_guard lock(mListenerMutex);
    if (mDataChannelListener) {
        mDataChannelListener->onDataChannelReceivedText(label, data);
    }
}

void PeerConnection::onSctpDataChannelBinary(const std::string& label, const ByteBuffer& data)
{
    std::lock_guard lock(mListenerMutex);
    if (mDataChannelListener) {
        mDataChannelListener->onDataChannelReceivedBinary(label, data);
    }
}

void PeerConnection::onSctpDataChannelClosed(const std::string& label)
{
    std::lock_guard lock(mListenerMutex);
    if (mDataChannelListener) {
        mDataChannelListener->onDataChannelClosed(label);
    }
}

void PeerConnection::sendReports()
{
    Task::cancelHelper(mTaskReports);
    mTaskReports = mLoopScheduler->submit(kReportsInterval, __FILE__, __LINE__, [this] { sendReports(); });

    if (mSelectedCandidate) {
        std::lock_guard lock(mMutex);

        if (mConnectionState == ConnectionState::Connected) {
            mSelectedCandidate->sendSenderReports();
            mSelectedCandidate->sendReceiverReports();
        }
    }
}

void PeerConnection::sendConnectionStats()
{
    Task::cancelHelper(mTaskConnectionStats);
    mTaskConnectionStats =
        mLoopScheduler->submit(kConnectionStatsInterval, __FILE__, __LINE__, [this] { sendConnectionStats(); });

    PublishConnectionStats publishConnectionStats = {};
    SubscribeConnectionStats subscribeConnectionStats = {};

    size_t subscribePacketsLost = 0;

    {
        std::lock_guard lock(mMutex);
        if (mConnectionState != ConnectionState::Connected) {
            return;
        }

        for (const auto& trackEntry : mTrackEntryList) {
            const auto trackItem = trackEntry.track;
            const auto stats = trackItem->getStats();

            if (trackItem->getDirection() == Direction::Publish) {
                publishConnectionStats.frame_count += stats->getSentFrames();
                publishConnectionStats.packet_count += stats->getSentPackets();
                publishConnectionStats.byte_count += stats->getSentBytes();
            } else if (trackItem->getDirection() == Direction::Subscribe) {
                subscribeConnectionStats.frame_count += stats->getReceivedFrames();
                subscribeConnectionStats.packet_count += stats->getReceivedPackets();
                subscribeConnectionStats.byte_count += stats->getReceivedBytes();
                subscribePacketsLost += stats->getReceivedPacketsLost();
            }
        }

        if (mSelectedCandidate) {
            mSelectedCandidate->updatePublishConnectionStats(publishConnectionStats);
            mSelectedCandidate->updateSubscribeConnectionStats(subscribeConnectionStats);
        }
    }

    if (subscribeConnectionStats.packet_count > 0 && subscribePacketsLost > 0) {
        subscribeConnectionStats.packets_lost_percent =
            static_cast<float>(static_cast<double>(subscribePacketsLost) * 100.0 /
                               static_cast<double>(subscribeConnectionStats.packet_count + subscribePacketsLost));
    }

    std::lock_guard lock(mListenerMutex);

    if (mPublishConnectionStatsListener && mDirection == Direction::Publish) {
        mPublishConnectionStatsListener(publishConnectionStats);
    }

    if (mSubscribeConnectionStatsListener && mDirection == Direction::Subscribe) {
        mSubscribeConnectionStatsListener(subscribeConnectionStats);
    }
}

void PeerConnection::sendPictureLossIndicator()
{
    if (mDirection == Direction::Subscribe) {
        {
            std::lock_guard lock(mMutex);
            const auto& config = mSdpOffer->getConfig();
            const auto interval = std::clamp<uint16_t>(config.pli_interval_millis, 500u, 4000u);

            Task::cancelHelper(mTaskPictureLossIndicator);
            mTaskPictureLossIndicator = mLoopScheduler->submit(
                std::chrono::milliseconds(interval), __FILE__, __LINE__, [this] { sendPictureLossIndicator(); });

            if (mConnectionState != ConnectionState::Connected) {
                return;
            }
        }

        if (mSelectedCandidate) {
            mSelectedCandidate->sendPictureLossIndicators();
        }
    }
}

} // namespace srtc

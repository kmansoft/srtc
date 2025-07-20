#include "srtc/peer_connection.h"
#include "srtc/depacketizer.h"
#include "srtc/event_loop.h"
#include "srtc/ice_agent.h"
#include "srtc/jitter_buffer.h"
#include "srtc/logging.h"
#include "srtc/packetizer.h"
#include "srtc/peer_candidate.h"
#include "srtc/rtcp_packet.h"
#include "srtc/rtcp_packet_source.h"
#include "srtc/rtp_time_source.h"
#include "srtc/sdp_answer.h"
#include "srtc/send_rtp_history.h"
#include "srtc/srtc.h"
#include "srtc/srtp_connection.h"
#include "srtc/track.h"
#include "srtc/track_stats.h"
#include "srtc/x509_certificate.h"

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

} // namespace

namespace srtc
{

PeerConnection::PeerConnection(Direction direction)
    : mDirection(direction)
    , mEventLoop(EventLoop::factory())
#ifdef NDEBUG
#else
    , mLosePacketsRandomGenerator(0, 99)
#endif
{
    std::call_once(gInitFlag, [] {
        // Just in case we need something
    });
}

PeerConnection::~PeerConnection()
{
    close();
}

std::pair<std::shared_ptr<SdpOffer>, Error> PeerConnection::createPublishOffer(
    const PubOfferConfig& pubConfig,
    const std::optional<PubVideoConfig>& videoConfig,
    const std::optional<PubAudioConfig>& audioConfig)
{
    if (mDirection != Direction::Publish) {
        return { {}, { Error::Code::InvalidData, "The peer connection's direction is not publish" } };
    }

    SdpOffer::Config config;
    config.cname = pubConfig.cname;
    config.enable_rtx = pubConfig.enable_rtx;
    config.enable_bwe = pubConfig.enable_bwe;
    config.debug_drop_packets = pubConfig.debug_drop_packets;

    std::optional<SdpOffer::VideoConfig> video;
    if (videoConfig) {
        video = SdpOffer::VideoConfig{};

        for (const auto& codec : videoConfig->codec_list) {
            video->codec_list.emplace_back(codec.codec, codec.profile_level_id);
        }
        for (const auto& layer : videoConfig->simulcast_layer_list) {
            const srtc::SimulcastLayer simulcastLayer = {
                layer.name, layer.width, layer.height, layer.frames_per_second, layer.kilobits_per_second
            };
            video->simulcast_layer_list.push_back(simulcastLayer);
        }
    }

    std::optional<SdpOffer::AudioConfig> audio;
    if (audioConfig) {
        audio = SdpOffer::AudioConfig{};

        for (const auto& codec : audioConfig->codec_list) {
            audio->codec_list.emplace_back(codec.codec, codec.minptime, codec.stereo);
        }
    }

    return { std::shared_ptr<SdpOffer>(new SdpOffer(Direction::Publish, config, video, audio)), Error::OK };
}

std::pair<std::shared_ptr<SdpOffer>, Error> PeerConnection::createSubscribeOffer(
    const SubOfferConfig& subConfig,
    const std::optional<SubVideoConfig>& videoConfig,
    const std::optional<SubAudioConfig>& audioConfig)
{
    if (mDirection != Direction::Subscribe) {
        return { {}, { Error::Code::InvalidData, "The peer connection's direction is not subscribe" } };
    }

    SdpOffer::Config config;
    config.cname = subConfig.cname;
    config.pli_interval_millis = subConfig.pli_interval_millis;
    config.jitter_buffer_length_millis = subConfig.jitter_buffer_length_millis;
    config.jitter_buffer_nack_delay_millis = subConfig.jitter_buffer_nack_delay_millis;
    config.debug_drop_packets = subConfig.debug_drop_packets;

    std::optional<SdpOffer::VideoConfig> video;
    if (videoConfig) {
        video = SdpOffer::VideoConfig{};

        for (const auto& codec : videoConfig->codec_list) {
            video->codec_list.emplace_back(codec.codec, codec.profile_level_id);
        }
    }

    std::optional<SdpOffer::AudioConfig> audio;
    if (audioConfig) {
        audio = SdpOffer::AudioConfig{};

        for (const auto& codec : audioConfig->codec_list) {
            audio->codec_list.emplace_back(codec.codec, codec.minptime, codec.stereo);
        }
    }

    return { std::shared_ptr<SdpOffer>(new SdpOffer(Direction::Subscribe, config, video, audio)), Error::OK };
}

Error PeerConnection::setOffer(const std::shared_ptr<SdpOffer>& offer)
{
    if (mDirection != offer->getDirection()) {
        return { Error::Code::InvalidData, "Wrong publish / subscribe direction for the offer" };
    }

    std::lock_guard lock(mMutex);

    assert(!mIsQuit);

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

    return SdpAnswer::parse(Direction::Publish, offer, answer, selector);
}

std::pair<std::shared_ptr<SdpAnswer>, Error> PeerConnection::parseSubscribeAnswer(
    const std::shared_ptr<SdpOffer>& offer, const std::string& answer, const std::shared_ptr<TrackSelector>& selector)
{
    if (mDirection != Direction::Subscribe) {
        return { {}, { Error::Code::InvalidData, "The peer connection's direction is not subscribe" } };
    }

    return SdpAnswer::parse(Direction::Subscribe, offer, answer, selector);
}

Error PeerConnection::setAnswer(const std::shared_ptr<SdpAnswer>& answer)
{
    std::lock_guard lock(mMutex);

    assert(!mIsQuit);

    if (mIsStarted) {
        return { Error::Code::InvalidData, "Connection is already started" };
    }

    mSdpAnswer = answer;

    mVideoSingleTrack = answer->getVideoSingleTrack();
    mVideoSimulcastTrackList = answer->getVideoSimulcastTrackList();
    mAudioTrack = answer->getAudioTrack();

    if (mSdpOffer && mSdpAnswer) {
        if (mSdpOffer->getDirection() != mSdpAnswer->getDirection()) {
            return { Error::Code::InvalidData, "Offer and answer must use same direction, publish or subscribe" };
        }

        if (mDirection == Direction::Publish) {
            // Packetizers
            if (mVideoSingleTrack) {
                const auto [packetizer, error] = Packetizer::make(mVideoSingleTrack);
                if (error.isError()) {
                    return error;
                }
                mVideoSinglePacketizer = packetizer;
            } else if (!mVideoSimulcastTrackList.empty()) {
                for (const auto& track : mVideoSimulcastTrackList) {
                    const auto simulcastLayer = track->getSimulcastLayer();

                    const auto [packetizer, error] = Packetizer::make(track);
                    if (error.isError()) {
                        return error;
                    }

                    mVideoSimulcastLayerList.emplace_back(simulcastLayer->name, track, packetizer);
                }
            }

            if (mAudioTrack) {
                const auto [packetizer, error] = Packetizer::make(mAudioTrack);
                if (error.isError()) {
                    return error;
                }
                mAudioPacketizer = packetizer;
            }
        } else if (mDirection == Direction::Subscribe) {
            // Jitter buffers are created when we connect because we have to know the rtt
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

std::shared_ptr<Track> PeerConnection::getVideoSingleTrack() const
{
    return mVideoSingleTrack;
}

std::vector<std::shared_ptr<Track>> PeerConnection::getVideoSimulcastTrackList() const
{
    return mVideoSimulcastTrackList;
}

std::shared_ptr<Track> PeerConnection::getAudioTrack() const
{
    return mAudioTrack;
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

Error PeerConnection::setVideoSingleCodecSpecificData(std::vector<ByteBuffer>&& list)
{
    std::lock_guard lock(mMutex);

    if (mDirection != Direction::Publish) {
        return { Error::Code::InvalidData, "The peer connection's direction is not publish" };
    }

    if (mVideoSingleTrack == nullptr) {
        return { Error::Code::InvalidData, "There is no video track" };
    }
    if (mVideoSinglePacketizer == nullptr) {
        return { Error::Code::InvalidData, "There is no video packetizer" };
    }

    mFrameSendQueue.push_back({ mVideoSingleTrack, mVideoSinglePacketizer, {}, std::move(list) });
    mEventLoop->interrupt();

    return Error::OK;
}

Error PeerConnection::publishVideoSingleFrame(ByteBuffer&& buf)
{
    if (mDirection != Direction::Publish) {
        return { Error::Code::InvalidData, "The peer connection's direction is not publish" };
    }

    std::lock_guard lock(mMutex);

    if (mConnectionState != ConnectionState::Connected) {
        return Error::OK;
    }

    if (mVideoSingleTrack == nullptr) {
        return { Error::Code::InvalidData, "There is no video track" };
    }
    if (mVideoSinglePacketizer == nullptr) {
        return { Error::Code::InvalidData, "There is no video packetizer" };
    }

    mFrameSendQueue.push_back({ mVideoSingleTrack, mVideoSinglePacketizer, std::move(buf) });
    mEventLoop->interrupt();

    return Error::OK;
}

Error PeerConnection::setVideoSimulcastCodecSpecificData(const std::string& layerName, std::vector<ByteBuffer>&& list)
{
    if (mDirection != Direction::Publish) {
        return { Error::Code::InvalidData, "The peer connection's direction is not publish" };
    }

    std::lock_guard lock(mMutex);

    const auto iter = std::find_if(mVideoSimulcastLayerList.begin(),
                                   mVideoSimulcastLayerList.end(),
                                   [&layerName](const auto& layer) { return layer.ridName == layerName; });
    if (iter == mVideoSimulcastLayerList.end()) {
        return { Error::Code::InvalidData, "There is no video layer named " + layerName };
    }

    const auto& layer = *iter;
    if (layer.track == nullptr) {
        return { Error::Code::InvalidData, "There is no video track" };
    }
    if (layer.packetizer == nullptr) {
        return { Error::Code::InvalidData, "There is no video packetizer" };
    }

    mFrameSendQueue.push_back({ layer.track, layer.packetizer, {}, std::move(list) });
    mEventLoop->interrupt();

    return Error::OK;
}

Error PeerConnection::publishVideoSimulcastFrame(const std::string& layerName, ByteBuffer&& buf)
{
    if (mDirection != Direction::Publish) {
        return { Error::Code::InvalidData, "The peer connection's direction is not publish" };
    }

    std::lock_guard lock(mMutex);

    if (mConnectionState != ConnectionState::Connected) {
        return Error::OK;
    }

    const auto iter = std::find_if(mVideoSimulcastLayerList.begin(),
                                   mVideoSimulcastLayerList.end(),
                                   [&layerName](const auto& layer) { return layer.ridName == layerName; });
    if (iter == mVideoSimulcastLayerList.end()) {
        return { Error::Code::InvalidData, "There is no video layer named " + layerName };
    }

    const auto& layer = *iter;
    if (layer.track == nullptr) {
        return { Error::Code::InvalidData, "There is no video track" };
    }
    if (layer.packetizer == nullptr) {
        return { Error::Code::InvalidData, "There is no video packetizer" };
    }

    mFrameSendQueue.push_back({ layer.track, layer.packetizer, std::move(buf) });
    mEventLoop->interrupt();

    return Error::OK;
}

Error PeerConnection::publishAudioFrame(ByteBuffer&& buf)
{
    if (mDirection != Direction::Publish) {
        return { Error::Code::InvalidData, "The peer connection's direction is not publish" };
    }

    std::lock_guard lock(mMutex);

    if (mConnectionState != ConnectionState::Connected) {
        return Error::OK;
    }

    if (mAudioTrack == nullptr) {
        return { Error::Code::InvalidData, "There is no audio track" };
    }
    if (mAudioPacketizer == nullptr) {
        return { Error::Code::InvalidData, "There is no audio packetizer" };
    }

    mFrameSendQueue.push_back({ mAudioTrack, mAudioPacketizer, std::move(buf) });
    mEventLoop->interrupt();

    return Error::OK;
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

    // We will be polling
    std::shared_ptr<EventLoop> eventLoop;
    {
        std::lock_guard lock(mMutex);
        eventLoop = mEventLoop;
    }

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
        if (mJitterBufferVideo) {
            if (const auto jitterTimeout = mJitterBufferVideo->getTimeoutMillis(kDefaultTimeoutMillis);
                timeout > jitterTimeout) {
                timeout = jitterTimeout;
            }
        }
        if (mJitterBufferAudio) {
            if (const auto jitterTimeout = mJitterBufferAudio->getTimeoutMillis(kDefaultTimeoutMillis);
                timeout > jitterTimeout) {
                timeout = jitterTimeout;
            }
        }

        std::vector<void*> udataList;
        eventLoop->wait(udataList, timeout);

        std::list<FrameToSend> frameSendQueue;

        {
            std::lock_guard lock(mMutex);
            if (mIsQuit) {
                break;
            }
            if (mConnectionState == ConnectionState::Failed) {
                break;
            }

            frameSendQueue = std::move(mFrameSendQueue);
        }

        // Read data from the network
        for (const auto udata : udataList) {
            if (udata) {
                // Read from socket
                const auto ptr = reinterpret_cast<PeerCandidate*>(udata);
                ptr->receiveFromSocket();
            }
        }

        // Scheduler
        mLoopScheduler->run();

        // Frames to send
        if (mSelectedCandidate) {
            for (auto& item : frameSendQueue) {
                mSelectedCandidate->addSendFrame(PeerCandidate::FrameToSend{
                    item.track, item.packetizer, std::move(item.buf), std::move(item.csd) });
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
        if (const auto buffer = mJitterBufferVideo) {
            processJitterBuffer(buffer);
        }
        if (const auto buffer = mJitterBufferAudio) {
            processJitterBuffer(buffer);
        }
    }

    mLoopScheduler.reset();

    setConnectionState(ConnectionState::Closed);

    // Clear everything on this thread before exiting
    mConnectingCandidateList.clear();
    mSelectedCandidate.reset();
    mJitterBufferVideo.reset();
    mJitterBufferAudio.reset();
}

void PeerConnection::setConnectionState(ConnectionState state)
{
    {
        std::lock_guard lock1(mMutex);

        if (mConnectionState == state) {
            // Already set
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

    auto connectDelay = 0;
    for (size_t i = 0; i < std::max(hostList4.size(), hostList6.size()); i += 1) {
        if (i < hostList4.size()) {
            const auto host = hostList4[i];
            const auto candidate = std::make_shared<PeerCandidate>(this,
                                                                   trackList,
                                                                   mSdpOffer,
                                                                   mSdpAnswer,
                                                                   mLoopScheduler,
                                                                   host,
                                                                   mEventLoop,
                                                                   std::chrono::milliseconds(connectDelay));
            mConnectingCandidateList.push_back(candidate);
        }
        if (i < hostList6.size()) {
            const auto host = hostList6[i];
            const auto candidate = std::make_shared<PeerCandidate>(this,
                                                                   trackList,
                                                                   mSdpOffer,
                                                                   mSdpAnswer,
                                                                   mLoopScheduler,
                                                                   host,
                                                                   mEventLoop,
                                                                   std::chrono::milliseconds(connectDelay));
            mConnectingCandidateList.push_back(candidate);
        }

        connectDelay += 100;
    }
}

void PeerConnection::processJitterBuffer(const std::shared_ptr<JitterBuffer>& buffer)
{
    const auto nackList = buffer->processNack();
    if (!nackList.empty()) {
        if (mSelectedCandidate) {
            mSelectedCandidate->sendNacks(buffer->getTrack(), nackList);
        }
    }

    const auto frameList = buffer->processDeque();
    if (!frameList.empty()) {
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

    if (const auto track = mVideoSingleTrack) {
        list.push_back(track);
    }
    for (const auto& item : mVideoSimulcastTrackList) {
        list.push_back(item);
    }
    if (const auto track = mAudioTrack) {
        list.push_back(track);
    }

    return list;
}

void PeerConnection::onCandidateHasDataToSend(PeerCandidate* candidate)
{
    std::lock_guard lock(mMutex);
    mEventLoop->interrupt();
}

void PeerConnection::onCandidateConnecting(PeerCandidate* candidate)
{
    setConnectionState(ConnectionState::Connecting);

    mJitterBufferVideo = nullptr;
    mJitterBufferAudio = nullptr;
}

void PeerConnection::onCandidateIceSelected(PeerCandidate* candidate)
{
    for (const auto& item : mConnectingCandidateList) {
        if (item.get() == candidate) {
            mSelectedCandidate = item;
            break;
        }
    }

    mConnectingCandidateList.clear();

    mLoopScheduler->dump();

    const auto trackList = collectTracks();
    for (const auto& trackItem : trackList) {
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

void PeerConnection::onCandidateConnected(PeerCandidate* candidate)
{
    setConnectionState(ConnectionState::Connected);

    if (mDirection == Direction::Subscribe) {
        const auto rtt = candidate->getIceRtt();
        if (rtt.has_value()) {
            std::lock_guard lock(mMutex);

            // We should have the rtt from ice
            auto length = std::chrono::milliseconds(lround(rtt.value()) + 12);
            auto nackDelay = std::chrono::milliseconds(6);

            if (rtt.value() >= 50.0f) {
                length = std::chrono::milliseconds(lround(rtt.value()) + 25);
                nackDelay = std::chrono::milliseconds(10);
            }

            const auto config = mSdpOffer->getConfig();
            if (config.jitter_buffer_length_millis > 0 && config.jitter_buffer_length_millis > length.count()) {
                length = std::chrono::milliseconds(config.jitter_buffer_length_millis);

                if (config.jitter_buffer_nack_delay_millis > 0 &&
                    config.jitter_buffer_nack_delay_millis > nackDelay.count()) {
                    nackDelay = std::chrono::milliseconds(config.jitter_buffer_nack_delay_millis);
                }
            }

            if (const auto track = mVideoSingleTrack) {
                const auto [depacketizer, error] = Depacketizer::make(track);
                if (error.isError()) {
                    LOG(SRTC_LOG_E, "Cannot create depacketizer for video: %s", error.mMessage.c_str());
                } else {
                    mJitterBufferVideo =
                        std::make_shared<JitterBuffer>(track, depacketizer, kJitterBufferSize, length, nackDelay);
                }
            }
            if (const auto track = mAudioTrack) {
                const auto [depacketizer, error] = Depacketizer::make(track);
                if (error.isError()) {
                    LOG(SRTC_LOG_E, "Cannot create depacketizer for audio: %s", error.mMessage.c_str());
                } else {
                    mJitterBufferAudio =
                        std::make_shared<JitterBuffer>(track, depacketizer, kJitterBufferSize, length, nackDelay);
                }
            }
        }

        sendPictureLossIndicator();
    }
}

void PeerConnection::onCandidateFailedToConnect(PeerCandidate* candidate, const Error& error)
{
    LOG(SRTC_LOG_E, "Candidate failed to connect: %d %s", static_cast<int>(error.mCode), error.mMessage.c_str());

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

void PeerConnection::onCandidateReceivedMediaPacket(PeerCandidate* candiate, const std::shared_ptr<RtpPacket>& packet)
{
#ifdef NDEBUG
#else
    {
        std::lock_guard lock(mMutex);

        const auto config = mSdpOffer->getConfig();
        const auto randomValue = mLosePacketsRandomGenerator.next();

        // In debug mode, we have deliberate 5% packet loss to validate that NACK / RTX processing works
        if (packet->getSSRC() == mSdpAnswer->getVideoSingleTrack()->getSSRC()) {
            if (config.debug_drop_packets && randomValue < 5) {
                if (mLosePacketHistory.shouldLosePacket(packet->getSSRC(), packet->getSequence())) {
                    LOG(SRTC_LOG_V,
                        "Dropping incoming packet with SSRC = %u, SEQ = %u",
                        packet->getSSRC(),
                        packet->getSequence());
                    return;
                }
            }
        } else if (packet->getSSRC() == mSdpAnswer->getVideoSingleTrack()->getRtxSSRC()) {
            ByteReader reader(packet->getPayload());
            const auto seq = reader.remaining() >= 2 ? reader.readU16() : 0;
            LOG(SRTC_LOG_V, "Received packet from RTX, SEQ = %u", seq);
        }
    }
#endif

    if (mDirection == Direction::Subscribe) {
        const auto track = packet->getTrack();

        const auto stats = track->getStats();
        stats->setHighestReceivedSeq(packet->getSequence());

        if (mVideoSingleTrack == track) {
            if (const auto buffer = mJitterBufferVideo) {
                assert(buffer->getTrack() == track);
                buffer->consume(packet);
            }
        } else if (mAudioTrack == track) {
            if (const auto buffer = mJitterBufferAudio) {
                assert(buffer->getTrack() == track);
                buffer->consume(packet);
            }
        }
    }
}

void PeerConnection::onCandidateReceivedSenderReport(PeerCandidate* candidate,
                                                     const std::shared_ptr<Track>& track,
                                                     const SenderReport& sr)
{
    std::lock_guard lock(mListenerMutex);
    if (mSubscribeSenderReportsListener) {
        mSubscribeSenderReportsListener(track, sr);
    }
}

void PeerConnection::sendReports()
{
    Task::cancelHelper(mTaskReports);
    mTaskReports = mLoopScheduler->submit(kReportsInterval, __FILE__, __LINE__, [this] { sendReports(); });

    if (mSelectedCandidate) {
        std::lock_guard lock(mMutex);

        if (mConnectionState == ConnectionState::Connected) {
            const auto trackList = collectTracks();
            mSelectedCandidate->sendSenderReports(trackList);
            mSelectedCandidate->sendReceiverReports(trackList);
        }
    }
}

void PeerConnection::sendConnectionStats()
{
    Task::cancelHelper(mTaskConnectionStats);
    mTaskConnectionStats =
        mLoopScheduler->submit(kConnectionStatsInterval, __FILE__, __LINE__, [this] { sendConnectionStats(); });

    PublishConnectionStats connectionStats = {};

    {
        connectionStats.packet_count = 0;
        connectionStats.byte_count = 0;
        connectionStats.packets_lost_percent = -1.0f;
        connectionStats.rtt_ms = -1.0f;
        connectionStats.bandwidth_actual_kbit_per_second = -1.0f;
        connectionStats.bandwidth_suggested_kbit_per_second = -1.0f;

        std::lock_guard lock(mMutex);
        if (mConnectionState != ConnectionState::Connected) {
            return;
        }

        const auto trackList = collectTracks();
        for (const auto& trackItem : trackList) {
            const auto stats = trackItem->getStats();
            connectionStats.packet_count += stats->getSentPackets();
            connectionStats.byte_count += stats->getSentBytes();
        }

        if (mSelectedCandidate) {
            mSelectedCandidate->updatePublishConnectionStats(connectionStats);
        }
    }

    std::lock_guard lock(mListenerMutex);
    if (mPublishConnectionStatsListener) {
        mPublishConnectionStatsListener(connectionStats);
    }
}

void PeerConnection::sendPictureLossIndicator()
{
    std::vector<std::shared_ptr<Track>> trackList;

    {
        std::lock_guard lock(mMutex);
        const auto& config = mSdpOffer->getConfig();

        Task::cancelHelper(mTaskPictureLossIndicator);
        mTaskPictureLossIndicator =
            mLoopScheduler->submit(std::chrono::milliseconds(config.pli_interval_millis), __FILE__, __LINE__, [this] {
                sendPictureLossIndicator();
            });

        if (mConnectionState != ConnectionState::Connected) {
            return;
        }

        trackList = collectTracks();
    }

    if (mSelectedCandidate) {
        mSelectedCandidate->sendPictureLossIndicators(trackList);
    }
}

} // namespace srtc

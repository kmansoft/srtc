#include "srtc/peer_connection.h"
#include "srtc/byte_buffer.h"
#include "srtc/event_loop.h"
#include "srtc/ice_agent.h"
#include "srtc/logging.h"
#include "srtc/packetizer.h"
#include "srtc/peer_candidate.h"
#include "srtc/rtcp_packet.h"
#include "srtc/rtcp_packet_source.h"
#include "srtc/rtp_time_source.h"
#include "srtc/scheduler.h"
#include "srtc/sdp_answer.h"
#include "srtc/sdp_offer.h"
#include "srtc/send_history.h"
#include "srtc/srtp_connection.h"
#include "srtc/track.h"
#include "srtc/track_stats.h"
#include "srtc/util.h"
#include "srtc/x509_certificate.h"

#include "stunmessage.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <unistd.h>

#define LOG(level, ...) srtc::log(level, "PeerConnection", __VA_ARGS__)

namespace
{

std::once_flag gInitFlag;

constexpr auto kSenderReportsInterval = std::chrono::seconds(1);
constexpr auto kConnectionStatsInterval = std::chrono::seconds(10);

} // namespace

namespace srtc
{

PeerConnection::PeerConnection()
    : mEventLoop(EventLoop::factory())
{
    std::call_once(gInitFlag, [] {
        // Just in case we need something
    });
}

PeerConnection::~PeerConnection()
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

std::shared_ptr<SdpOffer> PeerConnection::createPublishSdpOffer(const OfferConfig& config,
                                                                const srtc::optional<PubVideoConfig>& videoConfig,
                                                                const srtc::optional<PubAudioConfig>& audioConfig)
{
    return std::shared_ptr<SdpOffer>(new SdpOffer(config, videoConfig, audioConfig));
}

Error PeerConnection::setSdpOffer(const std::shared_ptr<SdpOffer>& offer)
{
    std::lock_guard lock(mMutex);

    assert(!mIsQuit);

    if (mIsStarted) {
        return { Error::Code::InvalidData, "Connection is already started" };
    }

    mSdpOffer = offer;

    return Error::OK;
}

std::pair<std::shared_ptr<SdpAnswer>, Error> PeerConnection::parsePublishSdpAnswer(
    const std::shared_ptr<SdpOffer>& offer, const std::string& answer, const std::shared_ptr<TrackSelector>& selector)
{
    return SdpAnswer::parse(offer, answer, selector);
}

Error PeerConnection::setSdpAnswer(const std::shared_ptr<SdpAnswer>& answer)
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
        // Packetizers
        if (mVideoSingleTrack) {
            const auto [packetizer, error] = Packetizer::makePacketizer(mVideoSingleTrack);
            if (error.isError()) {
                return error;
            }
            mVideoSinglePacketizer = packetizer;
        } else if (!mVideoSimulcastTrackList.empty()) {
            for (const auto& track : mVideoSimulcastTrackList) {
                const auto simulcastLayer = track->getSimulcastLayer();

                const auto [packetizer, error] = Packetizer::makePacketizer(track);
                if (error.isError()) {
                    return error;
                }

                mVideoSimulcastLayerList.emplace_back(simulcastLayer->name, track, packetizer);
            }
        }

        if (mAudioTrack) {
            const auto [packetizer, error] = Packetizer::makePacketizer(mAudioTrack);
            if (error.isError()) {
                return error;
            }
            mAudioPacketizer = packetizer;
        }

        // We are started
        mIsStarted = true;

        // The network thread
        mThread = std::thread(&PeerConnection::networkThreadWorkerFunc, this, mSdpOffer, mSdpAnswer);
    }

    return Error::OK;
}

std::shared_ptr<SdpOffer> PeerConnection::getSdpOffer() const
{
    std::lock_guard lock(mMutex);
    return mSdpOffer;
}

std::shared_ptr<SdpAnswer> PeerConnection::getSdpAnswer() const
{
    std::lock_guard lock(mMutex);
    return mSdpAnswer;
}

std::shared_ptr<Track> PeerConnection::getVideoSingleTrack() const
{
    std::lock_guard lock(mMutex);
    return mVideoSingleTrack;
}

std::vector<std::shared_ptr<Track>> PeerConnection::getVideoSimulcastTrackList() const
{
    std::lock_guard lock(mMutex);
    return mVideoSimulcastTrackList;
}

std::shared_ptr<Track> PeerConnection::getAudioTrack() const
{
    std::lock_guard lock(mMutex);
    return mAudioTrack;
}

void PeerConnection::setConnectionStateListener(const ConnectionStateListener& listener)
{
    std::lock_guard lock(mListenerMutex);
    mConnectionStateListener = listener;
}

void PeerConnection::setPublishConnectionStatListener(const PublishConnectionStatListener& listener)
{
    std::lock_guard lock(mListenerMutex);
    mPublishConnectionStatListener = listener;
}

Error PeerConnection::setVideoSingleCodecSpecificData(std::vector<ByteBuffer>&& list)
{
    std::lock_guard lock(mMutex);

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

void PeerConnection::networkThreadWorkerFunc(const std::shared_ptr<SdpOffer> offer,
                                             const std::shared_ptr<SdpAnswer> answer)
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
        std::vector<void*> udataList;
        eventLoop->wait(udataList, mLoopScheduler->getTimeoutMillis(1000));

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
            candidate->process();
            if (mConnectingCandidateList.empty()) {
                // A candidate reached ICE connected, and we removed all connecting ones
                break;
            }
        }

        if (mSelectedCandidate) {
            mSelectedCandidate->process();
        }
    }

    mLoopScheduler.reset();

    setConnectionState(ConnectionState::Closed);

    // Clear everything on this thread before quitting
    mConnectingCandidateList.clear();
    mSelectedCandidate.reset();
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
        } else if (mConnectionState == ConnectionState::Connected) {
            const auto trackList = collectTracksLocked();
            for (const auto& trackItem : trackList) {
                trackItem->getStats()->clear();

                trackItem->getRtpPacketSource()->clear();
                trackItem->getRtxPacketSource()->clear();
                trackItem->getRtcpPacketSource()->clear();
            }

            Task::cancelHelper(mTaskSenderReports);
            mTaskSenderReports =
                mLoopScheduler->submit(kSenderReportsInterval, __FILE__, __LINE__, [this] { sendSenderReports(); });

            Task::cancelHelper(mTaskConnectionStats);
            mTaskConnectionStats =
                mLoopScheduler->submit(kConnectionStatsInterval, __FILE__, __LINE__, [this] { sendConnectionStats(); });
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
            const auto candidate = std::make_shared<PeerCandidate>(
                this, mSdpOffer, mSdpAnswer, mLoopScheduler, host, mEventLoop, std::chrono::milliseconds(connectDelay));
            mConnectingCandidateList.push_back(candidate);
        }
        if (i < hostList6.size()) {
            const auto host = hostList6[i];
            const auto candidate = std::make_shared<PeerCandidate>(
                this, mSdpOffer, mSdpAnswer, mLoopScheduler, host, mEventLoop, std::chrono::milliseconds(connectDelay));
            mConnectingCandidateList.push_back(candidate);
        }

        connectDelay += 100;
    }
}

std::vector<std::shared_ptr<Track>> PeerConnection::collectTracksLocked() const
{
    std::vector<std::shared_ptr<Track>> list;

    if (mConnectionState == ConnectionState::Connected) {
        if (const auto track = mVideoSingleTrack) {
            list.push_back(track);
        }
        for (const auto& item : mVideoSimulcastTrackList) {
            list.push_back(item);
        }
        if (const auto track = mAudioTrack) {
            list.push_back(track);
        }
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
}

void PeerConnection::onCandidateDtlsConnected(PeerCandidate* candidate)
{
    setConnectionState(ConnectionState::Connected);
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

void PeerConnection::onCandidateLostConnection(srtc::PeerCandidate* candidate, const srtc::Error& error)
{
    LOG(SRTC_LOG_E, "Candidate lost connection: %d %s", static_cast<int>(error.mCode), error.mMessage.c_str());

    // We are currently connected, the candidate lost connection and then failed to re-establish, so start connecting
    // again
    mSelectedCandidate.reset();
    startConnecting();
}

void PeerConnection::sendSenderReports()
{
    Task::cancelHelper(mTaskSenderReports);
    mTaskSenderReports =
        mLoopScheduler->submit(kSenderReportsInterval, __FILE__, __LINE__, [this] { sendSenderReports(); });

    std::lock_guard lock(mMutex);

    if (mConnectionState == ConnectionState::Connected) {
        const auto trackList = collectTracksLocked();
        for (const auto& trackItem : trackList) {
            ByteBuffer payload;
            ByteWriter w(payload);

            // https://www4.cs.fau.de/Projects/JRTP/pmt/node83.html

            NtpTime ntp = {};
            getNtpTime(ntp);

            const auto timeSource = trackItem->getRtpTimeSource();
            const auto rtpTime = timeSource->getCurrTimestamp();

            w.writeU32(ntp.seconds);
            w.writeU32(ntp.fraction);
            w.writeU32(rtpTime);

            const auto stats = trackItem->getStats();
            w.writeU32(stats->getSentPackets());
            w.writeU32(stats->getSentBytes());

            const auto packet =
                std::make_shared<RtcpPacket>(trackItem, 0, RtcpPacket::kSenderReport, std::move(payload));
            if (mSelectedCandidate) {
                mSelectedCandidate->sendRtcpPacket(packet);
            }
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
        std::lock_guard lock(mMutex);
        if (mConnectionState != ConnectionState::Connected) {
            return;
        }

        const auto trackList = collectTracksLocked();
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
    if (mPublishConnectionStatListener) {
        mPublishConnectionStatListener(connectionStats);
    }
}
} // namespace srtc

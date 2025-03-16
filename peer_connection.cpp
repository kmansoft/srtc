#include "srtc/peer_connection.h"
#include "srtc/peer_candidate.h"
#include "srtc/sdp_offer.h"
#include "srtc/sdp_answer.h"
#include "srtc/track.h"
#include "srtc/byte_buffer.h"
#include "srtc/logging.h"
#include "srtc/x509_certificate.h"
#include "srtc/scheduler.h"
#include "srtc/packetizer.h"
#include "srtc/ice_agent.h"
#include "srtc/send_history.h"
#include "srtc/srtp_connection.h"

#include "stunmessage.h"

#include <cassert>
#include <chrono>
#include <unistd.h>

#include <sys/eventfd.h>
#include <sys/epoll.h>

#define LOG(level, ...) srtc::log(level, "PeerConnection", __VA_ARGS__)

namespace srtc {

PeerConnection::PeerConnection() = default;

PeerConnection::~PeerConnection()
{
    std::thread waitForThread;

    {
        std::lock_guard lock(mMutex);

        if (mIsStarted) {
            mIsQuit = true;

            eventfd_write(mEventHandle, 1);
            waitForThread = std::move(mThread);
        }
    }

    if (waitForThread.joinable()) {
        waitForThread.join();
    }

    {
        std::lock_guard lock(mMutex);

        if (mEventHandle >= 0) {
            close(mEventHandle);
            mEventHandle = -1;
        }
    }
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

Error PeerConnection::setSdpAnswer(const std::shared_ptr<SdpAnswer>& answer)
{
    std::lock_guard lock(mMutex);

    assert(!mIsQuit);

    if (mIsStarted) {
        return { Error::Code::InvalidData, "Connection is already started" };
    }

    mSdpAnswer = answer;

    mVideoTrack = answer->getVideoTrack();
    mAudioTrack = answer->getAudioTrack();

    if (mSdpOffer && mSdpAnswer) {
        // Packetizers
        if (mVideoTrack) {
            const auto [packetizer, error] = Packetizer::makePacketizer(mVideoTrack->getCodec());
            if (error.isError()) {
                return error;
            }
            mVideoPacketizer = packetizer;
        }
        if (mAudioTrack) {
            const auto [packetizer, error] = Packetizer::makePacketizer(mAudioTrack->getCodec());
            if (error.isError()) {
                return error;
            }
            mAudioPacketizer = packetizer;
        }

        // We are started
        mIsStarted = true;

        // Event handle for talking to the network thread and the network thread itself
        mEventHandle = eventfd(0, EFD_NONBLOCK);
        mEpollHandle = epoll_create(1);
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

std::shared_ptr<Track> PeerConnection::getVideoTrack() const
{
    std::lock_guard lock(mMutex);
    return mVideoTrack;
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

Error PeerConnection::setVideoCodecSpecificData(std::vector<ByteBuffer>& list)
{
    std::lock_guard lock(mMutex);

    if (mVideoTrack == nullptr) {
        return { Error::Code::InvalidData, "There is no video track" };
    }
    if (mVideoPacketizer == nullptr) {
        return { Error::Code::InvalidData, "There is no video packetizer" };
    }

    mFrameSendQueue.push_back({
      mVideoTrack,
      mVideoPacketizer,
      { },
      std::move(list)
    });
    eventfd_write(mEventHandle, 1);

    return Error::OK;
}

Error PeerConnection::publishVideoFrame(ByteBuffer&& buf)
{
    std::lock_guard lock(mMutex);

    if (mConnectionState != ConnectionState::Connected) {
        return Error::OK;
    }

    if (mVideoTrack == nullptr) {
        return { Error::Code::InvalidData, "There is no video track" };
    }
    if (mVideoPacketizer == nullptr) {
        return { Error::Code::InvalidData, "There is no video packetizer" };
    }

    mFrameSendQueue.push_back({
        mVideoTrack,
        mVideoPacketizer,
        std::move(buf)
    });
    eventfd_write(mEventHandle, 1);

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

    mFrameSendQueue.push_back({
                                      mAudioTrack,
                                      mAudioPacketizer,
                                      std::move(buf)
                              });
    eventfd_write(mEventHandle, 1);

    return Error::OK;
}

void PeerConnection::networkThreadWorkerFunc(const std::shared_ptr<SdpOffer> offer,
                                             const std::shared_ptr<SdpAnswer> answer)
{
    // Loop scheduler
    mLoopScheduler = std::make_shared<LoopScheduler>();

    // We will be polling, the size arg is obsolete
    int epollHandle;
    {
        std::lock_guard lock(mMutex);

        epollHandle = mEpollHandle;

        struct epoll_event ev = {  };

        ev.events = EPOLLIN;
        ev.data.fd = 0;

        epoll_ctl(mEpollHandle, EPOLL_CTL_ADD, mEventHandle, &ev);
    }

    // We are connecting
    startConnecting();

    // Our processing loop
    while (true) {
        // Epoll for incoming data
        struct epoll_event epollEvent[10];
        const auto nfds = epoll_wait(epollHandle, epollEvent,
                                     sizeof(epollEvent) / sizeof(epollEvent[0]),
                                     mLoopScheduler->getTimeoutMillis(1000));

        std::list<PeerCandidate::FrameToSend> frameSendQueue;

        {
            std::lock_guard lock(mMutex);
            if (mIsQuit) {
                break;
            }
            if (mConnectionState == ConnectionState::Failed) {
                break;
            }

            frameSendQueue = std::move(mFrameSendQueue);

            for (int i = 0; i < nfds; i += 1) {
                if (epollEvent[i].data.fd == 0) {
                    // Read from event
                    eventfd_t value = {0};
                    eventfd_read(mEventHandle, &value);
                } else if (epollEvent[i].data.ptr != nullptr) {
                    // Read from socket
                    const auto ptr = reinterpret_cast<PeerCandidate *>(epollEvent[i].data.ptr);
                    ptr->receiveFromSocket();
                }
            }
        }

        // Scheduler
        mLoopScheduler->run();

        // Frames to send
        if (mSelectedCandidate) {
            for (auto& item: frameSendQueue) {
                mSelectedCandidate->addSendFrame(
                        PeerCandidate::FrameToSend{
                                item.track,
                                item.packetizer,
                                std::move(item.buf),
                                std::move(item.csd)
                        }
                );
            }
        }

        // Candidate processing
        for (const auto& candidate: mConnectingCandidateList) {
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

    {
        std::lock_guard lock(mMutex);
        close(mEpollHandle);
        mEpollHandle = -1;
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
            eventfd_write(mEventHandle, 1);
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
    for (size_t i = 0; i < std::max(hostList4.size(), hostList6.size()); i +=1) {
        if (i < hostList4.size()) {
            const auto host = hostList4[i];
            const auto candidate = std::make_shared<PeerCandidate>(
                    this,
                    mSdpOffer, mSdpAnswer,
                    mLoopScheduler,
                    host,
                    mEpollHandle,
                    std::chrono::milliseconds(connectDelay));
            mConnectingCandidateList.push_back(candidate);
        }
        if (i < hostList6.size()) {
            const auto host = hostList6[i];
            const auto candidate = std::make_shared<PeerCandidate>(
                    this,
                    mSdpOffer, mSdpAnswer,
                    mLoopScheduler,
                    host,
                    mEpollHandle,
                    std::chrono::milliseconds(connectDelay));
            mConnectingCandidateList.push_back(candidate);
       }

        connectDelay += 100;
    }
}

void PeerConnection::onCandidateHasDataToSend(PeerCandidate* candidate)
{
    std::lock_guard lock(mMutex);
    eventfd_write(mEventHandle, 1);
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
    LOG(SRTC_LOG_E, "Candidate failed to connect: %d %s", error.mCode, error.mMessage.c_str());

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

void PeerConnection::onCandidateLostConnection(srtc::PeerCandidate *candidate, const srtc::Error &error)
{
    LOG(SRTC_LOG_E, "Candidate lost connection: %d %s", error.mCode, error.mMessage.c_str());

    // We are currently connected, the candidate lost connection and then failed to re-establish, so start connecting again
    mSelectedCandidate.reset();
    startConnecting();
}

}

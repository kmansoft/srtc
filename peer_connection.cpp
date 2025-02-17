#include "srtc/peer_connection.h"
#include "srtc/peer_candidate.h"
#include "srtc/sdp_offer.h"
#include "srtc/sdp_answer.h"
#include "srtc/track.h"
#include "srtc/byte_buffer.h"
#include "srtc/util.h"
#include "srtc/logging.h"
#include "srtc/x509_certificate.h"
#include "srtc/scheduler.h"
#include "srtc/packetizer.h"
#include "srtc/socket.h"
#include "srtc/ice_agent.h"
#include "srtc/send_history.h"
#include "srtc/srtp_connection.h"

#include "stunmessage.h"

#include <cassert>

#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include <srtp.h>

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
    // We are connecting
    setConnectionState(ConnectionState::Connecting);

    // Candidate
    const auto host = answer->getHostList()[0]; // TODO try all candidates
    const auto candidate = std::make_unique<PeerCandidate>(
            this,
            offer, answer,
            host);

    // Our socket loop
    const auto epollHandle = epoll_create(2);

    {
        std::lock_guard lock(mMutex);

        struct epoll_event ev = { .events = EPOLLIN };

        ev.data.fd = 0;
        epoll_ctl(epollHandle, EPOLL_CTL_ADD, mEventHandle, &ev);

        ev.data.ptr = candidate.get();
        epoll_ctl(epollHandle, EPOLL_CTL_ADD, candidate->getSocketFd(), &ev);
    }

    // Loop scheduler
    const auto scheduler = std::make_unique<LoopScheduler>();

    // Our processing loop
    while (true) {
        // Epoll for incoming data
        struct epoll_event epollEvent[10];
        const auto nfds = epoll_wait(epollHandle, epollEvent,
                                     sizeof(epollEvent) / sizeof(epollEvent[0]),
                                     scheduler->getTimeoutMillis(1000));

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
        scheduler->run();

        // Frames to send
        for (auto& item : frameSendQueue) {
            candidate->addSendFrame(
                    PeerCandidate::FrameToSend{
                        item.track,
                        item.packetizer,
                        std::move(item.buf),
                        std::move(item.csd)
                    }
            );
        }

        // Candidate processing
        candidate->process();
    }

    close(epollHandle);

    setConnectionState(ConnectionState::Closed);
}

void PeerConnection::setConnectionState(ConnectionState state)
{
    {
        std::lock_guard lock1(mMutex);

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
    // TODO: Forget all candidates except the one that succeeded
}

void PeerConnection::onCandidateDtlsConnected(PeerCandidate* candidate)
{
    setConnectionState(ConnectionState::Connected);
}

void PeerConnection::onCandidateFailed(PeerCandidate* candidate, const Error& error)
{
    LOG(SRTC_LOG_E, "Candidate failed: %d %s", error.mCode, error.mMessage.c_str());
    setConnectionState(ConnectionState::Failed);
}

}

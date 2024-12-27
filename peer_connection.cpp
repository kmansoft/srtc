#include "srtc/peer_connection.h"
#include "srtc/sdp_answer.h"
#include "srtc/track.h"
#include "srtc/byte_buffer.h"

#include <cassert>
#include <iostream>

#include <sys/eventfd.h>
#include <sys/epoll.h>

#include <openssl/x509.h>
#include <openssl/bio.h>

namespace srtc {

PeerConnection::PeerConnection()
{
}

PeerConnection::~PeerConnection()
{
    std::thread waitForThread;

    {
        std::lock_guard lock(mMutex);

        if (mThread.joinable()) {
            mState = State::Deactivating;
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
        if (mSocketHandle >= 0) {
            close(mSocketHandle);
            mSocketHandle = -1;
        }
    }
}

void PeerConnection::setSdpOffer(const std::shared_ptr<SdpOffer>& offer)
{
    std::lock_guard lock(mMutex);

    assert(mState == State::Inactive);

    mSdpOffer = offer;
}

void PeerConnection::setSdpAnswer(const std::shared_ptr<SdpAnswer>& answer)
{
    std::lock_guard lock(mMutex);

    assert(mState == State::Inactive);

    mSdpAnswer = answer;

    mVideoTrack = answer->getVideoTrack();
    mAudioTrack = answer->getAudioTrack();

    if (mSdpOffer && mSdpAnswer) {
        mDestHost = answer->getHostList()[0];

        mEventHandle = eventfd(0, EFD_NONBLOCK);

        mSocketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        const auto socketFlags = fcntl(mSocketHandle, F_GETFL, 0);
        fcntl(mSocketHandle, F_SETFL, socketFlags | O_NONBLOCK);

        mThread = std::thread(&PeerConnection::networkThreadWorkerFunc, this);
    }
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

void PeerConnection::networkThreadWorkerFunc()
{
    auto epollHandle = epoll_create(2);

    {
        std::lock_guard lock(mMutex);

        struct epoll_event ev = { .events = EPOLLIN };

        epoll_ctl(epollHandle, EPOLL_CTL_ADD, mEventHandle, &ev);
        epoll_ctl(epollHandle, EPOLL_CTL_ADD, mSocketHandle, &ev);
    }

    while (true) {
        struct epoll_event epollEvent[2];
        const auto nfds = epoll_wait(epollHandle, epollEvent, 2, -1);

        uint8_t recv_buf[2048];
        size_t recv_n = 0;


        {
            std::lock_guard lock(mMutex);
            if (mState == State::Deactivating) {
                break;
            }

            while (!mSendQueue.empty()) {
                const auto buf = mSendQueue.front();
                mSendQueue.pop();

                struct sockaddr_in destAddr = {
                        .sin_family = AF_INET,
                        .sin_port = htons(mDestHost.port),
                        .sin_addr = mDestHost.host.ipv4
                };

                sendto(mSocketHandle, buf->data(), buf->len(),
                       0,
                        (struct sockaddr *) &destAddr, sizeof(destAddr));
            }

            for (int i = 0; i < nfds; i += 1) {
                if (epollEvent[i].data.fd == mEventHandle) {
                    // Read from event
                    eventfd_t value = { 0 };
                    eventfd_read(mEventHandle, &value);
                } else if (epollEvent[i].data.fd == mSocketHandle) {
                    // Read from socket
                    struct iovec iov = { .iov_base  = recv_buf, .iov_len = sizeof(recv_buf) };
                    struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1 };

                    const auto r = recvmsg(mSocketHandle, &mh, 0);
                    if (r > 0) {
                        std::cout << "Received " << r << " bytes" << std::endl;

                        recv_n = r;
                    }
                }
            }
        }

        if (recv_n > 0) {
            ByteBuffer buf = { recv_buf, recv_n };
        }
    }

    close(epollHandle);
    epollHandle = -1;
}

}

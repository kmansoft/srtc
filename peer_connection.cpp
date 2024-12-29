#include "srtc/peer_connection.h"
#include "srtc/sdp_offer.h"
#include "srtc/sdp_answer.h"
#include "srtc/track.h"
#include "srtc/byte_buffer.h"
#include "srtc/x509_certificate.h"

#include <cassert>
#include <iostream>

#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <openssl/x509.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/dtls1.h>

#include "stunagent.h"
#include "stunmessage.h"

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

        mThread = std::thread(&PeerConnection::networkThreadWorkerFunc, this, mSdpOffer, mSdpAnswer, mDestHost);

        // mThread = std::thread(&PeerConnection::networkThreadDtlsTestFunc, this, mSdpOffer, mDestHost);
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

union anyaddr {
    struct sockaddr_storage ss;
    struct sockaddr_in sin_ipv4;
    struct sockaddr_in6 sin_ipv6;
};

void PeerConnection::networkThreadWorkerFunc(const std::shared_ptr<SdpOffer> offer,
                                             const std::shared_ptr<SdpAnswer> answer,
                                             const Host host)
{
    // Dest socket address
    struct sockaddr_in destAddr = {
            .sin_family = AF_INET,
            .sin_port = htons(mDestHost.port),
            .sin_addr = mDestHost.host.ipv4
    };

    // Send an ICE BINDING message to socket

    uint8_t iceMessageBuffer[2048];

    StunAgent agent = {};
    stun_agent_init(&agent, STUN_ALL_KNOWN_ATTRIBUTES,
                    STUN_COMPATIBILITY_RFC5389,
                    static_cast<StunAgentUsageFlags>(
                            STUN_AGENT_USAGE_SHORT_TERM_CREDENTIALS |
                            STUN_AGENT_USAGE_USE_FINGERPRINT
                    ));

    StunMessage msg = {};
    stun_agent_init_request (&agent, &msg, iceMessageBuffer, sizeof(iceMessageBuffer), STUN_BINDING);

    const uint32_t priority = 1847591679u;
    stun_message_append32 (&msg, STUN_ATTRIBUTE_PRIORITY, priority);

    const uint64_t tie = ((uint64_t)mrand48()) << 32 | mrand48();
    stun_message_append64 (&msg, STUN_ATTRIBUTE_ICE_CONTROLLING, tie);

    const auto offerUserName = offer->getIceUFrag();
    const auto answerUserName = answer->getIceUFrag();
    const auto iceUserName = answerUserName + ":" + offerUserName;
    const auto icePassword = answer->getIcePassword();

    stun_message_append_string(&msg, STUN_ATTRIBUTE_USERNAME,
                               iceUserName.c_str());
    const auto iceMessageSize = stun_agent_finish_message (&agent, &msg,
                               reinterpret_cast<const uint8_t*>(icePassword.data()), icePassword.size());

    if (stun_message_has_attribute(&msg, STUN_ATTRIBUTE_USERNAME)) {
        printf("Yes USERNAME");
    }
    if (stun_message_has_attribute(&msg, STUN_ATTRIBUTE_MESSAGE_INTEGRITY)) {
        printf("Yes INTEGRITY");
    }
    if (stun_message_has_attribute(&msg, STUN_ATTRIBUTE_FINGERPRINT)) {
        printf("Yes FINGERPRINT");
    }

    {
        std::lock_guard lock(mMutex);

        mSendQueue.emplace_back(iceMessageBuffer, iceMessageSize);
        eventfd_write(mEventHandle, 1);
    }

    // Our socket loop

    auto epollHandle = epoll_create(2);

    {
        std::lock_guard lock(mMutex);

        struct epoll_event ev = { .events = EPOLLIN };

        ev.data.fd = mEventHandle;
        epoll_ctl(epollHandle, EPOLL_CTL_ADD, mEventHandle, &ev);

        ev.data.fd = mSocketHandle;
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
                const auto buf = std::move(mSendQueue.front());
                mSendQueue.erase(mSendQueue.begin());

                const auto w = sendto(mSocketHandle, buf.data(), buf.len(),
                       0,
                        (struct sockaddr *) &destAddr, sizeof(destAddr));
                if (w < 0) {
                    printf("Failed to send\n");
                }
            }

            for (int i = 0; i < nfds; i += 1) {
                if (epollEvent[i].data.fd == mEventHandle) {
                    // Read from event
                    eventfd_t value = { 0 };
                    eventfd_read(mEventHandle, &value);
                } else if (epollEvent[i].data.fd == mSocketHandle) {
                    // Read from socket
                    union anyaddr from = { };
                    socklen_t fromLen = sizeof(from);

                    const auto r = recvfrom(mSocketHandle, recv_buf, sizeof(recv_buf), 0,
                                            reinterpret_cast<struct sockaddr*>(&from), &fromLen);
                    if (r > 0) {
                        std::cout << "Received " << r << " bytes" << std::endl;

                        recv_n = r;
                    }
                }
            }
        }

        if (recv_n > 0) {
            ByteBuffer buf = { recv_buf, recv_n };

            if (recv_n >= 20) {
                uint32_t magic = htonl(0x2112A442);
                uint8_t cookie[4];
                std::memcpy(cookie, recv_buf + 4, 4);

                if (std::memcmp(&magic, cookie, 4) == 0) {
                    std::cout << "The STUN magic cookie is present" << std::endl;

                    StunMessage stunMessage = {
                            .buffer = recv_buf,
                            .buffer_len = recv_n
                    };

                    const auto stunMessageClass = stun_message_get_class(&stunMessage);
                    const auto stunMessageMethod = stun_message_get_method(&stunMessage);

                    std::cout << "STUN message class  =" << stunMessageClass << std::endl;
                    std::cout << "STUN message method = " << stunMessageMethod << std::endl;
                }
            }
        }
    }

    close(epollHandle);
    epollHandle = -1;
}

// Custom BIO for DGRAM

struct dgram_data {
    struct sockaddr_in sin;
};

static bool bio_socket_should_retry(ssize_t ret) {
    if (ret != -1) {
        return false;
    }

    return errno == EWOULDBLOCK ||
        errno == EINTR;
}

static int dgram_read(BIO *b, char *out, int outl)
{
    if (out == nullptr) {
        return 0;
    }

    union anyaddr from = { };
    socklen_t fromLen = sizeof(from);

    const auto ret = recvfrom(b->num, out, outl,
                       0,
                       reinterpret_cast<struct sockaddr*>(&from), &fromLen);

    BIO_clear_retry_flags(b);
    if (ret <= 0) {
        if (bio_socket_should_retry(ret)) {
            BIO_set_retry_read(b);
        }
    }
    return static_cast<int>(ret);
}

static int dgram_write(BIO *b, const char *in, int inl) {
    auto data = reinterpret_cast<dgram_data*>(b->ptr);

    const auto ret = sendto(b->num, in, inl, 0,
                            reinterpret_cast<const struct sockaddr*>(&data->sin), sizeof(data->sin));

    BIO_clear_retry_flags(b);
    if (ret <= 0) {
        if (bio_socket_should_retry(ret)) {
            BIO_set_retry_write(b);
        }
    }
    return static_cast<int>(ret);
}

#define BIO_C_SET_ADDR 1000

static long dgram_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    auto data = reinterpret_cast<dgram_data*>(b->ptr);

    switch (cmd) {
        case BIO_C_SET_FD:
            if (b->num && b->shutdown) {
                close(b->num);
            }
            b->num = *((int *)ptr);
            b->shutdown = (int)num;
            b->init = 1;
            break;
        case BIO_C_SET_ADDR:
            std::memcpy(&data->sin, ptr, num);
            break;
        default:
            break;
    }

    return 1;
}

static int dgram_free(BIO *bio)
{
    if (bio->shutdown) {
        if (bio->init) {
            close(bio->num);
        }
        bio->init = 0;
        bio->flags = 0;
    }
    auto data = reinterpret_cast<dgram_data*>(bio->ptr);
    delete data;
    return 1;
}

static const BIO_METHOD methods_dgram = {
        BIO_TYPE_DGRAM, "dgram",
        dgram_write,      dgram_read,
        nullptr /* puts */, nullptr /* gets, */,
        dgram_ctrl,       nullptr /* create */,
        dgram_free,       nullptr /* callback_ctrl */,
};

BIO *BIO_new_dgram(int fd, int close_flag) {
    BIO *ret;

    ret = BIO_new(&methods_dgram);
    if (ret == nullptr) {
        return nullptr;
    }
    BIO_set_fd(ret, fd, close_flag);

    ret->ptr = new dgram_data{};

    return ret;
}

static int verify_callback(int ok, X509_STORE_CTX *store_ctx) {
    return 1;
}

void PeerConnection::networkThreadDtlsTestFunc(const std::shared_ptr<SdpOffer> offer,
                                               const Host host)
{
    const auto cert = offer->getCertificate();
    const auto ctx = SSL_CTX_new(DTLS_client_method());

    SSL_CTX_use_certificate(ctx, cert->getCertificate());
    SSL_CTX_use_PrivateKey(ctx, cert->getPrivateKey());

    if (!SSL_CTX_check_private_key (ctx)) {
        printf("ERROR: invalid private key");
        return;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, verify_callback);
//    SSL_CTX_set_read_ahead(ctx, 1);

    const auto fd = socket(AF_INET, SOCK_DGRAM, 0);
    const auto ssl = SSL_new(ctx);
    const auto bio = BIO_new_dgram(fd, 0);

    SSL_set_bio(ssl, bio, bio);

    auto r1 = SSL_set_tlsext_use_srtp(ssl, "SRTP_AES128_CM_SHA1_80");

    const struct sockaddr_in sin {
        .sin_family = AF_INET,
        .sin_port = htons(3478),
        .sin_addr = host.host.ipv4
    };

    BIO_ctrl(bio, BIO_C_SET_ADDR, sizeof(sin), (void*) &sin);

    auto r2 = SSL_connect(ssl);

    close(fd);
    BIO_free(bio);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
}

}

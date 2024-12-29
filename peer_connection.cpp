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

#ifdef ANDROID
#include <android/log.h>
#include <cstdarg>
static void LOG(const char* format...)  __attribute__ ((format (printf, 1, 2)))
{
    va_list ap;
    va_start(ap, format);
    __android_log_vprint(ANDROID_LOG_INFO, "PeerConnection", format, ap);
    va_end(ap);

}
#else
#include <cstdio>
#define LOG(format, ...) printf(format "\n", __VA_ARGS__)
#endif

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

struct ReceivedData {
    ByteBuffer buf;
    anyaddr addr;
    size_t addr_len;
};

// https://datatracker.ietf.org/doc/html/rfc5389#section-6
constexpr auto kStunRfc5389Cookie = 0x2112A442;

// https://datatracker.ietf.org/doc/html/rfc5245#section-4.1.2.1
uint32_t make_stun_priority(int type_preference,
                            int local_preference,
                            uint8_t component_id)
{
    return
        (1 << 24) * type_preference +
        (1 << 8) * local_preference +
                (256 - component_id);
}

StunMessage make_stun_message_binding_request(StunAgent* agent,
                                              uint8_t* buf,
                                              size_t len,
                                              const std::shared_ptr<SdpOffer>& offer,
                                              const std::shared_ptr<SdpAnswer>& answer)
{
    StunMessage msg = {};
    stun_agent_init_request (agent, &msg, buf, len, STUN_BINDING);

    const uint32_t priority = make_stun_priority(200, 10, 1);
    stun_message_append32 (&msg, STUN_ATTRIBUTE_PRIORITY, priority);

    const uint64_t tie = ((uint64_t)lrand48()) << 32 | lrand48();
    stun_message_append64 (&msg, STUN_ATTRIBUTE_ICE_CONTROLLING, tie);

    // https://datatracker.ietf.org/doc/html/rfc5245#section-7.1.2.3
    const auto offerUserName = offer->getIceUFrag();
    const auto answerUserName = answer->getIceUFrag();
    const auto iceUserName = answerUserName + ":" + offerUserName;
    const auto icePassword = answer->getIcePassword();

    stun_message_append_string(&msg, STUN_ATTRIBUTE_USERNAME,
                               iceUserName.c_str());
    stun_agent_finish_message (agent, &msg,
                               reinterpret_cast<const uint8_t*>(icePassword.data()), icePassword.size());

    return msg;
}

bool is_stun_message(const ByteBuffer& buf)
{
    if (buf.len() > 20) {
        uint32_t magic = htonl(kStunRfc5389Cookie);
        uint8_t cookie[4];
        std::memcpy(cookie, buf.data() + 4, 4);

        if (std::memcmp(&magic, cookie, 4) == 0) {
            return true;
        }
    }

    return false;
}

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

    // We will be using STUN
    StunAgent iceAgent = {};
    uint8_t iceMessageBuffer[2048];

    stun_agent_init(&iceAgent, STUN_ALL_KNOWN_ATTRIBUTES,
                    STUN_COMPATIBILITY_RFC5389,
                    static_cast<StunAgentUsageFlags>(
                            STUN_AGENT_USAGE_SHORT_TERM_CREDENTIALS |
                            STUN_AGENT_USAGE_USE_FINGERPRINT
                    ));

    // Send an ICE STUN BINDING message
    const auto iceMessageBindingRequest = make_stun_message_binding_request(&iceAgent,
                                                                            iceMessageBuffer, sizeof(iceMessageBuffer),
                                                                            offer, answer);
    {
        std::lock_guard lock(mMutex);

        mSendQueue.emplace_back(iceMessageBuffer, stun_message_length(&iceMessageBindingRequest));
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

    // Receive buffer
    constexpr auto kReceiveBufferSize = 2048;
    const auto receiveBuffer = std::make_unique<uint8_t[]>(kReceiveBufferSize);

    while (true) {
        // Receive queue
        std::list<ReceivedData> receiveQueue;

        // Epoll
        struct epoll_event epollEvent[2];
        const auto nfds = epoll_wait(epollHandle, epollEvent, 2, -1);

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
                LOG("Sent %zd bytes", w);
            }

            for (int i = 0; i < nfds; i += 1) {
                if (epollEvent[i].data.fd == mEventHandle) {
                    // Read from event
                    eventfd_t value = { 0 };
                    eventfd_read(mEventHandle, &value);
                } else if (epollEvent[i].data.fd == mSocketHandle) {
                    // Read from socket
                    while (true) {
                        union anyaddr from = {};
                        socklen_t fromLen = sizeof(from);

                        const auto r = recvfrom(mSocketHandle, receiveBuffer.get(), kReceiveBufferSize, 0,
                                                reinterpret_cast<struct sockaddr *>(&from),
                                                &fromLen);
                        if (r > 0) {
                            LOG("Received %zd bytes", r);

                            receiveQueue.emplace_back(ByteBuffer(receiveBuffer.get(), r), from, fromLen);
                        } else {
                            break;
                        }
                    }
                }
            }
        }

        while (!receiveQueue.empty()) {
            const ReceivedData data = std::move(receiveQueue.front());
            receiveQueue.erase(receiveQueue.begin());

            if (is_stun_message(data.buf)) {
                LOG("Received STUN message");

                StunMessage incomingMessage = {
                        .buffer = data.buf.data(),
                        .buffer_len = data.buf.len()
                };

                const auto stunMessageClass = stun_message_get_class(&incomingMessage);
                const auto stunMessageMethod = stun_message_get_method(&incomingMessage);

                LOG("STUN message class  = %d", stunMessageClass);
                LOG("STUN message method = %d", stunMessageMethod);

                if (stunMessageClass == STUN_REQUEST && stunMessageMethod == STUN_BINDING) {
                    StunMessage responseMessage = {};
                    stun_agent_init_response(&iceAgent, &responseMessage, iceMessageBuffer, sizeof(iceMessageBuffer),
                                             &incomingMessage);
                    stun_message_append_xor_addr (&responseMessage, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS,
                                                  &data.addr.ss, sizeof(data.addr.ss));

                    uint16_t requestUserNameLen = { };
                    const auto requestUserName = stun_message_find (&incomingMessage,
                                                                STUN_ATTRIBUTE_USERNAME, &requestUserNameLen);
                    if (requestUserName) {
                        std::string userName { static_cast<const char*>(requestUserName), requestUserNameLen };

                        stun_message_append_bytes (&responseMessage, STUN_ATTRIBUTE_USERNAME,
                                                   requestUserName, requestUserNameLen);
                    }

                    stun_agent_finish_message(&iceAgent, &responseMessage, nullptr, 0);

                    std::lock_guard lock(mMutex);

                    mSendQueue.emplace_back(iceMessageBuffer, stun_message_length(&responseMessage));
                    eventfd_write(mEventHandle, 1);
                } else if (stunMessageClass == STUN_RESPONSE && stunMessageMethod == STUN_BINDING) {
                    int errorCode = { };
                    if (stun_message_find_error(&incomingMessage, &errorCode) == STUN_MESSAGE_RETURN_SUCCESS) {
                        LOG("STUN response error code: %d", errorCode);
                    }

                    uint8_t id[STUN_MESSAGE_TRANS_ID_LEN];
                    stun_message_id(&incomingMessage, id);

                    if (stun_agent_forget_transaction(&iceAgent, id)) {
                        LOG("Removed old transaction ID for binding request");
                    }
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

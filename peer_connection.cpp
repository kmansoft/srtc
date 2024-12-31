#include "srtc/peer_connection.h"
#include "srtc/sdp_offer.h"
#include "srtc/sdp_answer.h"
#include "srtc/track.h"
#include "srtc/byte_buffer.h"
#include "srtc/util.h"
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

static void LOG(const char* format...)  __attribute__ ((format (printf, 1, 2)));

static void LOG(const char* format...)
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
        auto socketFlags = fcntl(mSocketHandle, F_GETFL, 0);
        socketFlags |= O_NONBLOCK;
        fcntl(mSocketHandle, F_SETFL, socketFlags);

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
                                              const std::shared_ptr<SdpAnswer>& answer,
                                              bool useCandidate)
{
    StunMessage msg = {};
    stun_agent_init_request (agent, &msg, buf, len, STUN_BINDING);

    if (useCandidate) {
        stun_message_append_flag(&msg, STUN_ATTRIBUTE_USE_CANDIDATE);
    }

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

// Custom BIO for DGRAM

struct dgram_data {
    PeerConnection* pc;
};

int PeerConnection::dgram_read(BIO *b, char *out, int outl)
{
    if (out == nullptr) {
        return 0;
    }

    BIO_clear_retry_flags(b);

    auto data = reinterpret_cast<dgram_data *>(b->ptr);

    std::lock_guard lock(data->pc->mMutex);

    if (data->pc->mDtlsReceiveQueue.empty()) {
        BIO_set_retry_read(b);
        return -1;
    }

    const auto item = std::move(data->pc->mDtlsReceiveQueue.front());
    data->pc->mDtlsReceiveQueue.erase(data->pc->mDtlsReceiveQueue.begin());

    const auto ret = std::min(static_cast<int>(item.len()), outl);
    std::memcpy(out, item.data(), static_cast<size_t>(ret));

    return ret;
}

int PeerConnection::dgram_write(BIO *b, const char *in, int inl) {
    auto data = reinterpret_cast<dgram_data*>(b->ptr);

    data->pc->enqueueForSending({
        reinterpret_cast<const uint8_t *>(in),
        static_cast<size_t>(inl) });

    return inl;
}

long PeerConnection::dgram_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    return 1;
}

int PeerConnection::dgram_free(BIO *b)
{
    auto data = reinterpret_cast<dgram_data*>(b->ptr);
    delete data;
    return 1;
}

const BIO_METHOD PeerConnection::dgram_method = {
        BIO_TYPE_DGRAM, "dgram",
        PeerConnection::dgram_write,      PeerConnection::dgram_read,
        nullptr /* puts */, nullptr /* gets, */,
        PeerConnection::dgram_ctrl,       nullptr /* create */,
        PeerConnection::dgram_free,       nullptr /* callback_ctrl */,
};

BIO *PeerConnection::BIO_new_dgram(PeerConnection* pc) {
    BIO *b = BIO_new(&dgram_method);
    if (b == nullptr) {
        return nullptr;
    }

    BIO_set_init(b, 1);
    BIO_set_shutdown(b, 0);

    b->ptr = new dgram_data{
        .pc = pc,
    };
    return b;
}

static int verify_callback(int ok, X509_STORE_CTX *store_ctx) {
    // We verify cert after the handshake has completed
    return 1;
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

    // States
    auto iceState = IceState::Inactive;
    auto dtlsState = DtlsState::Inactive;

    // DTLS
    SSL_CTX* dtls_ctx = {};
    SSL* dtls_ssl = {};
    BIO* dtls_bio = {};

    // We will be using STUN
    const auto iceAgent = std::make_unique<StunAgent>();

    constexpr auto kIceMessageBufferSize = 2048;
    const auto iceMessageBuffer = std::make_unique<uint8_t[]>(kIceMessageBufferSize);

    const auto iceAgentSize = sizeof(iceAgent);

    stun_agent_init(iceAgent.get(), STUN_ALL_KNOWN_ATTRIBUTES,
                    STUN_COMPATIBILITY_RFC5389,
                    static_cast<StunAgentUsageFlags>(
                            STUN_AGENT_USAGE_SHORT_TERM_CREDENTIALS |
                            STUN_AGENT_USAGE_USE_FINGERPRINT
                    ));

    auto sentUseCandidate = false;

    {
        const auto iceMessageBindingRequest1 = make_stun_message_binding_request(iceAgent.get(),
                                                                                 iceMessageBuffer.get(),
                                                                                 kIceMessageBufferSize,
                                                                                 offer, answer,
                                                                                 sentUseCandidate);
        enqueueForSending({
            iceMessageBuffer.get(), stun_message_length(&iceMessageBindingRequest1)
        });
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
            ReceivedData data = std::move(receiveQueue.front());
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
                    stun_agent_init_response(iceAgent.get(), &responseMessage,
                                             iceMessageBuffer.get(), kIceMessageBufferSize,
                                             &incomingMessage);
                    stun_message_append_xor_addr (&responseMessage, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS,
                                                  &data.addr.ss, sizeof(data.addr.ss));

                    uint16_t requestUserNameLen = { };
                    const auto requestUserName = stun_message_find (&incomingMessage,
                                                                STUN_ATTRIBUTE_USERNAME, &requestUserNameLen);
                    if (requestUserName) {
                        stun_message_append_bytes (&responseMessage, STUN_ATTRIBUTE_USERNAME,
                                                   requestUserName, requestUserNameLen);
                    }

                    uint8_t id[STUN_MESSAGE_TRANS_ID_LEN];
                    stun_message_id(&incomingMessage, id);

                    const auto icePassword = offer->getIcePassword();
                    stun_agent_finish_message(iceAgent.get(), &responseMessage,
                                              reinterpret_cast<const uint8_t*>(icePassword.data()), icePassword.size());

                    enqueueForSending({ iceMessageBuffer.get(), stun_message_length(&responseMessage) });
                } else if (stunMessageClass == STUN_RESPONSE && stunMessageMethod == STUN_BINDING) {
                    int errorCode = { };
                    if (stun_message_find_error(&incomingMessage, &errorCode) == STUN_MESSAGE_RETURN_SUCCESS) {
                        LOG("STUN response error code: %d", errorCode);
                    }

                    uint8_t id[STUN_MESSAGE_TRANS_ID_LEN];
                    stun_message_id(&incomingMessage, id);

                    if (stun_agent_forget_transaction(iceAgent.get(), id)) {
                        LOG("Removed old transaction ID for binding request");

                        if (!sentUseCandidate) {
                            sentUseCandidate = true;

                            const auto iceMessageBindingRequest2 = make_stun_message_binding_request(
                                    iceAgent.get(),
                                    iceMessageBuffer.get(),
                                    kIceMessageBufferSize,
                                    offer, answer,
                                    sentUseCandidate);
                            enqueueForSending({
                                iceMessageBuffer.get(),
                                stun_message_length(&iceMessageBindingRequest2)});

                            iceState = IceState::Completed;
                            dtlsState = DtlsState::Activating;
                        }
                    }
                }
            } else {
                // Received non-STUN message
                LOG("Received non-STUN message %zd, %d", data.buf.len(), data.buf.data()[0]);

                {
                    std::lock_guard lock(mMutex);
                    mDtlsReceiveQueue.push_back(std::move(data.buf));
                }

                // Try the handshake
                if (dtls_ssl) {
                    if (dtlsState == DtlsState::Activating) {
                        const auto r1 = SSL_do_handshake(dtls_ssl);
                        const auto err = SSL_get_error(dtls_ssl, r1);
                        LOG("DTLS handshake: %d, %d", r1, err);

                        if (err == SSL_ERROR_WANT_READ) {
                            LOG("Still in progress");
                        } else if (r1 == 1 && err == 0) {
                            const auto cipher = SSL_get_cipher(dtls_ssl);
                            const auto profile = SSL_get_selected_srtp_profile(dtls_ssl);

                            LOG("Completed with cipher %s, profile %s", cipher, profile->name);

                            X509 *cert = SSL_get_peer_certificate(dtls_ssl);
                            if (cert != nullptr) {
                                uint8_t fpBuf[32] = {};
                                unsigned int fpSize = {};

                                const auto digest = EVP_get_digestbyname("sha256");
                                X509_digest(cert, digest, fpBuf, &fpSize);

                                std::string hex = bin_to_hex(fpBuf, fpSize);
                                LOG("Remote certificate sha-256: %s", hex.c_str());

                                const auto expectedHash = answer->getCertificateHash();
                                const ByteBuffer actualHashBin = {fpBuf, fpSize};

                                if (expectedHash.getBin() == actualHashBin) {
                                    LOG("Remote certificate matches the SDP");
                                    dtlsState = DtlsState::Completed;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (dtlsState == DtlsState::Activating && dtls_ssl == nullptr) {
            LOG("Preparing for the DTLS handshake");

            const auto cert = offer->getCertificate();
            dtls_ctx = SSL_CTX_new(DTLS_client_method());

            SSL_CTX_use_certificate(dtls_ctx, cert->getCertificate());
            SSL_CTX_use_PrivateKey(dtls_ctx, cert->getPrivateKey());

            if (!SSL_CTX_check_private_key(dtls_ctx)) {
                LOG("ERROR: invalid private key");
            }

            SSL_CTX_set_verify(dtls_ctx, SSL_VERIFY_PEER, verify_callback);

            SSL_CTX_set_min_proto_version(dtls_ctx, DTLS1_VERSION);
            SSL_CTX_set_max_proto_version(dtls_ctx, DTLS1_3_VERSION);
            SSL_CTX_set_read_ahead(dtls_ctx, 1);

            dtls_ssl = SSL_new(dtls_ctx);

            dtls_bio = BIO_new_dgram(this);

            SSL_set_bio(dtls_ssl, dtls_bio, dtls_bio);

            SSL_set_tlsext_use_srtp(dtls_ssl,
                                    "SRTP_AEAD_AES_256_GCM:SRTP_AEAD_AES_128_GCM:SRTP_AES128_CM_SHA1_80");
            SSL_set_connect_state(dtls_ssl);

            if (answer->isSetupActive()) {
                SSL_set_accept_state(dtls_ssl);
            } else {
                SSL_set_connect_state(dtls_ssl);
                SSL_do_handshake(dtls_ssl);
            }
        }
    }

    if (dtls_ssl) {
        SSL_shutdown(dtls_ssl);
    }

    SSL_free(dtls_ssl);
    SSL_CTX_free(dtls_ctx);

    close(epollHandle);
    epollHandle = -1;
}

void PeerConnection::enqueueForSending(ByteBuffer&& buf)
{
    LOG("Enqueing %zd bytes", buf.len());

    std::lock_guard lock(mMutex);
    mSendQueue.push_back(std::move(buf));
    eventfd_write(mEventHandle, 1);
}

}

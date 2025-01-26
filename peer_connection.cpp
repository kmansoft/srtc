#include "srtc/peer_connection.h"
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

#include <srtp.h>

#include "stunagent.h"
#include "stunmessage.h"

#define LOG(...) srtc::log("PeerConnection", __VA_ARGS__)

namespace {

constexpr auto kTag = "PeerConnection";

// GCM support requires libsrtp to be build with OpenSSL which is what we do
constexpr auto kSrtpCipherList = "SRTP_AEAD_AES_128_GCM:SRTP_AEAD_AES_256_GCM:SRTP_AES128_CM_SHA1_80";

std::once_flag gSrtpInitFlag;

struct dgram_data {
    srtc::PeerConnection* pc;
};

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

StunMessage make_stun_message_binding_request(const std::unique_ptr<srtc::IceAgent>& agent,
                                              uint8_t* buf,
                                              size_t len,
                                              const std::shared_ptr<srtc::SdpOffer>& offer,
                                              const std::shared_ptr<srtc::SdpAnswer>& answer,
                                              bool useCandidate)
{
    StunMessage msg = {};
    // stun_agent_init_request (agent, &msg, buf, len, STUN_BINDING);
    agent->initRequest(&msg, buf, len, STUN_BINDING);

    if (useCandidate) {
        stun_message_append_flag(&msg, STUN_ATTRIBUTE_USE_CANDIDATE);
    }

    const uint32_t priority = make_stun_priority(200, 10, 1);
    stun_message_append32 (&msg, STUN_ATTRIBUTE_PRIORITY, priority);

    // https://datatracker.ietf.org/doc/html/rfc5245#section-7.1.2.3
    const auto offerUserName = offer->getIceUFrag();
    const auto answerUserName = answer->getIceUFrag();
    const auto iceUserName = answerUserName + ":" + offerUserName;
    const auto icePassword = answer->getIcePassword();

    agent->finishMessage(&msg, iceUserName, icePassword);

    return msg;
}

StunMessage make_stun_message_binding_response(const std::unique_ptr<srtc::IceAgent>& agent,
                                               uint8_t* buf,
                                               size_t len,
                                               const std::shared_ptr<srtc::SdpOffer>& offer,
                                               const std::shared_ptr<srtc::SdpAnswer>& answer,
                                               const StunMessage& request,
                                               const srtc::anyaddr& address,
                                               socklen_t addressLen)
{
    StunMessage msg = {};
    agent->initResponse(&msg, buf, len, &request);

    stun_message_append_xor_addr (&msg, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS,
                                  &address.ss, addressLen);

    // https://datatracker.ietf.org/doc/html/rfc5245#section-7.1.2.3
    const auto offerUserName = offer->getIceUFrag();
    const auto answerUserName = answer->getIceUFrag();
    const auto iceUserName = offerUserName + ":" + answerUserName;
    const auto icePassword = offer->getIcePassword();

    agent->finishMessage(&msg, iceUserName, icePassword);

    return msg;
}

bool is_stun_message(const srtc::ByteBuffer& buf)
{
    // https://datatracker.ietf.org/doc/html/rfc5764#section-5.1.2
    if (buf.size() > 20) {
        const auto data =  buf.data();
        if (data[0] < 2) {
            uint32_t magic = htonl(srtc::IceAgent::kRfc5389Cookie);
            uint8_t cookie[4];
            std::memcpy(cookie, buf.data() + 4, 4);

            if (std::memcmp(&magic, cookie, 4) == 0) {
                return true;
            }
        }
    }

    return false;
}

bool is_dtls_message(const srtc::ByteBuffer& buf) {
    // https://datatracker.ietf.org/doc/html/rfc7983#section-5
    if (buf.size() >= 4) {
        const auto data = buf.data();
        if (data[0] >= 20 && data[0] <= 24) {
            return true;
        }
    }

    return false;
}

bool is_rtc_message(const srtc::ByteBuffer& buf) {
    // https://datatracker.ietf.org/doc/html/rfc3550#section-5.1
    if (buf.size() >= 8) {
        const auto data = buf.data();
        if (data[0] >= 128 && data[0] <= 191) {
            return true;
        }
    }

    return false;
}

bool is_rtcp_message(const srtc::ByteBuffer& buf) {
    // https://datatracker.ietf.org/doc/html/rfc5761#section-4
    if (buf.size() >= 8) {
        const auto data = buf.data();
        const auto payloadId = data[1] & 0x7F;
        return payloadId >= 64 && payloadId <= 95;
    }
    return false;
}

long long elapsedMillis(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

// test code

template <class TScheduler>
void testScheduler(const std::unique_ptr<TScheduler>& scheduler,
                   const std::string& label)
{
    const auto start1000 = std::chrono::steady_clock::now();
    scheduler->submit(std::chrono::milliseconds(1000), [label, start1000] {
        const auto message = label + ": Task at 1000 ms";
        LOG("%s, at %lld ms", message.c_str(), elapsedMillis(start1000));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto start2000 = std::chrono::steady_clock::now();
    scheduler->submit(std::chrono::milliseconds(2000), [label, start2000] {
        const auto message = label + ": Task at 2000 ms";
        LOG("%s, at %lld ms", message.c_str(), elapsedMillis(start2000));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto start500 = std::chrono::steady_clock::now();
    scheduler->submit(std::chrono::milliseconds(500), [label, start500] {
        const auto message = label + ": Task at 500 ms";
        LOG("%s, at %lld ms", message.c_str(), elapsedMillis(start500));
    });
}

}

namespace srtc {

PeerConnection::PeerConnection()
    : mScheduler(std::make_unique<ThreadScheduler>("srtc-pc"))
{
    // Test code
    testScheduler(mScheduler, "ThreadScheduler");
}

PeerConnection::~PeerConnection()
{
    std::thread waitForThread;

    {
        std::lock_guard lock(mMutex);

        mIsQuit = true;

        if (mThread.joinable()) {
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

void PeerConnection::setSdpOffer(const std::shared_ptr<SdpOffer>& offer)
{
    std::lock_guard lock(mMutex);

    assert(!mThread.joinable() && !mIsQuit);

    mSdpOffer = offer;
}

Error PeerConnection::setSdpAnswer(const std::shared_ptr<SdpAnswer>& answer)
{
    std::lock_guard lock(mMutex);

    assert(!mThread.joinable() && !mIsQuit);

    mSdpAnswer = answer;

    mVideoTrack = answer->getVideoTrack();
    mAudioTrack = answer->getAudioTrack();

    if (mVideoTrack) {
        mVideoTrack->setSSRC(mSdpOffer->getVideoSSRC());
    }
    if (mAudioTrack) {
        mAudioTrack->setSSRC(mSdpOffer->getAudioSSRC());
    }

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

        // Event handle for talking to the network thread and the network thread itself
        mEventHandle = eventfd(0, EFD_NONBLOCK);
        mThread = std::thread(&PeerConnection::networkThreadWorkerFunc, this, mSdpOffer, mSdpAnswer);

        // mThread = std::thread(&PeerConnection::networkThreadDtlsTestFunc, this, mSdpOffer, mDestHost);
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

    mFrameSendQueue.push_back(FrameToSend{
      .track = mVideoTrack,
      .packetizer = mVideoPacketizer,
      .buf = ByteBuffer(),
      .csd = std::move(list)
    });
    eventfd_write(mEventHandle, 1);

    return Error::OK;
}

Error PeerConnection::publishVideoFrame(ByteBuffer& buf)
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
        .track = mVideoTrack,
        .packetizer = mVideoPacketizer,
        .buf = std::move(buf)
    });
    eventfd_write(mEventHandle, 1);

    return Error::OK;
}

// Custom BIO for DGRAM

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

    const auto ret = std::min(static_cast<int>(item.size()), outl);
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
                                             const std::shared_ptr<SdpAnswer> answer)
{
    // Init the SRTP library
    std::call_once(gSrtpInitFlag, []{
        srtp_init();
    });

    // We are connecting
    setConnectionState(ConnectionState::Connecting);

    // Dest host and its socket address
    const auto destHost = answer->getHostList()[0]; // TODO try all candidates
    LOG("Connecting to %s", to_string(destHost.addr).c_str());

    // Dest socket
    auto socket = std::make_unique<Socket>(destHost.addr);

    // States
    auto dtlsState = DtlsState::Inactive;

    // DTLS
    SSL_CTX* dtls_ctx = {};
    SSL* dtls_ssl = {};
    BIO* dtls_bio = {};

    // SRTP
    srtp_t srtp_in = { nullptr }, srtp_out = {nullptr };
    ByteBuffer srtp_client_key_buf, srtp_server_key_buf;

    // ICE negotiation
    const auto iceAgent = std::make_unique<IceAgent>();

    constexpr auto kIceMessageBufferSize = 2048;
    const auto iceMessageBuffer = std::make_unique<uint8_t[]>(kIceMessageBufferSize);

    auto sentUseCandidate = false;

    {
        const auto iceMessageBindingRequest1 = make_stun_message_binding_request(iceAgent,
                                                                                 iceMessageBuffer.get(),
                                                                                 kIceMessageBufferSize,
                                                                                 offer, answer,
                                                                                 false);
        enqueueForSending({
            iceMessageBuffer.get(), stun_message_length(&iceMessageBindingRequest1)
        });
    }

    // Send history
    const auto sendHistory = std::make_unique<SendHistory>();

    // Our socket loop
    auto eventFd = -1;
    auto epollHandle = epoll_create(2);

    {
        std::lock_guard lock(mMutex);

        eventFd = mEventHandle;

        struct epoll_event ev = { .events = EPOLLIN };

        ev.data.fd = eventFd;
        epoll_ctl(epollHandle, EPOLL_CTL_ADD, eventFd, &ev);

        ev.data.fd = socket->fd();
        epoll_ctl(epollHandle, EPOLL_CTL_ADD, socket->fd(), &ev);
    }

    // Loop scheduler
    const auto scheduler = std::make_unique<LoopScheduler>();
    testScheduler(scheduler, "LoopScheduler");

    // Receive buffer
    while (true) {
        // Receive queue
        std::list<Socket::ReceivedData> receiveQueue;

        // Epoll
        struct epoll_event epollEvent[2];
        const auto nfds = epoll_wait(epollHandle, epollEvent, 2,
                                     scheduler->getTimeoutMillis());

        std::list<ByteBuffer> rawSendQueue;
        std::list<FrameToSend> frameSendQueue;

        {
           std::lock_guard lock(mMutex);
            if (mIsQuit) {
                break;
            }

            rawSendQueue = std::move(mRawSendQueue);
            frameSendQueue = std::move(mFrameSendQueue);

            for (int i = 0; i < nfds; i += 1) {
                if (epollEvent[i].data.fd == eventFd) {
                    // Read from event
                    eventfd_t value = { 0 };
                    eventfd_read(mEventHandle, &value);
                } else if (epollEvent[i].data.fd == socket->fd()) {
                    // Read from socket
                    auto receiveList = socket->receive();
                    for (auto& item : receiveList) {
                        LOG("Received %zd bytes", item.buf.size());
                        receiveQueue.emplace_back(std::move(item));
                    }
                }
            }
        }

        // Scheduler
        scheduler->run();

        // Raw data
        while (!rawSendQueue.empty()) {
            const auto buf = std::move(rawSendQueue.front());
            rawSendQueue.erase(rawSendQueue.begin());

            const auto w = socket->send(buf);
            LOG("Sent %zd raw bytes", w);
        }

        // Frames
        while (!frameSendQueue.empty()) {
            const auto item = std::move(frameSendQueue.front());
            frameSendQueue.erase(frameSendQueue.begin());

            if (!item.csd.empty()) {
                // Codec Specific Data
                item.packetizer->setCodecSpecificData(item.csd);
            } else if (!item.buf.empty()) {
                // Frame data
                const auto packetList = item.packetizer->generate(item.track, item.buf);
                for (const auto& packet : packetList) {
                    // Save
                    sendHistory->save(packet);

                    // Generate
                    auto packetData = packet->generate();

                    // Encrypt
                    void* rtp_header = packetData.data();
                    int rtp_size_1 = static_cast<int>(packetData.size());
                    int rtp_size_2 = rtp_size_1;

                    packetData.padding(SRTP_MAX_TRAILER_LEN);

                    const auto protectStatus = srtp_protect(srtp_out, rtp_header, &rtp_size_2);
                    if (protectStatus == srtp_err_status_ok) {
                        assert(rtp_size_2 > rtp_size_1);

                        // And send
                        const auto randomValue = lrand48() % 100;
                        if (randomValue <= 5) {
                            LOG("NOT sending an RTP packet with SSRC = %u, SEQ = %u", packet->getSSRC(), packet->getSequence());
                        } else {
                            (void) socket->send(rtp_header, rtp_size_2);
                        }
                    } else {
                        LOG("Error applying SRTP protection: %d", protectStatus);
                    }
                }
            }
        }

        // Receive
        while (!receiveQueue.empty()) {
            Socket::ReceivedData data = std::move(receiveQueue.front());
            receiveQueue.erase(receiveQueue.begin());

            if (is_stun_message(data.buf)) {
                LOG("Received STUN message");

                StunMessage incomingMessage = { };
                incomingMessage.buffer = data.buf.data();
                incomingMessage.buffer_len = data.buf.size();

                const auto stunMessageClass = stun_message_get_class(&incomingMessage);
                const auto stunMessageMethod = stun_message_get_method(&incomingMessage);

                LOG("STUN message class  = %d", stunMessageClass);
                LOG("STUN message method = %d", stunMessageMethod);

                if (stunMessageClass == STUN_REQUEST && stunMessageMethod == STUN_BINDING) {
                    const auto iceMessageBindingResponse = make_stun_message_binding_response(
                            iceAgent,
                            iceMessageBuffer.get(),
                            kIceMessageBufferSize,
                            offer, answer,
                            incomingMessage,
                            data.addr, data.addr_len
                            );
                    enqueueForSending({ iceMessageBuffer.get(), stun_message_length(&iceMessageBindingResponse) });
                } else if (stunMessageClass == STUN_RESPONSE && stunMessageMethod == STUN_BINDING) {
                    int errorCode = { };
                    if (stun_message_find_error(&incomingMessage, &errorCode) == STUN_MESSAGE_RETURN_SUCCESS) {
                        LOG("STUN response error code: %d", errorCode);
                    }

                    uint8_t id[STUN_MESSAGE_TRANS_ID_LEN];
                    stun_message_id(&incomingMessage, id);

                    if (/* TODO stun_agent_forget_transaction(stunAgent.get(), id) || */ true /* debug */) {
                        LOG("Removed old transaction ID for binding request");

                        if (!sentUseCandidate) {
                            sentUseCandidate = true;

                            const auto iceMessageBindingRequest2 = make_stun_message_binding_request(
                                    iceAgent,
                                    iceMessageBuffer.get(),
                                    kIceMessageBufferSize,
                                    offer, answer,
                                    true);
                            enqueueForSending({
                                iceMessageBuffer.get(),
                                stun_message_length(&iceMessageBindingRequest2)});

                            dtlsState = DtlsState::Activating;
                        }
                    }
                }
            } else if (is_dtls_message(data.buf)) {
                LOG("Received DTLS message %zd, %d", data.buf.size(), data.buf.data()[0]);

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

                            const auto cert = SSL_get_peer_certificate(dtls_ssl);
                            if (cert == nullptr) {
                                // Error, no certificate
                                LOG("There is no DTLS server certificate");
                                dtlsState = DtlsState::Failed;
                                setConnectionState(ConnectionState::Failed);
                            } else {
                                uint8_t fpBuf[32] = {};
                                unsigned int fpSize = {};

                                const auto digest = EVP_get_digestbyname("sha256");
                                X509_digest(cert, digest, fpBuf, &fpSize);

                                std::string hex = bin_to_hex(fpBuf, fpSize);
                                LOG("Remote certificate sha-256: %s", hex.c_str());

                                const auto expectedHash = answer->getCertificateHash();
                                const auto actualHashBin = ByteBuffer {fpBuf, fpSize};

                                if (expectedHash.getBin() == actualHashBin) {
                                    LOG("Remote certificate matches the SDP");

                                    // https://stackoverflow.com/questions/22692109/webrtc-srtp-decryption
                                    const auto srtpProfileName = SSL_get_selected_srtp_profile(dtls_ssl);

                                    srtp_profile_t srtpProfile;
                                    size_t srtpKeySize = { 0 }, srtpSaltSize = { 0 };

                                    switch (srtpProfileName->id) {
                                        case SRTP_AEAD_AES_256_GCM:
                                            srtpProfile = srtp_profile_aead_aes_256_gcm;
                                            srtpKeySize = SRTP_AES_256_KEY_LEN;
                                            srtpSaltSize = SRTP_AEAD_SALT_LEN;
                                            break;
                                        case SRTP_AEAD_AES_128_GCM:
                                            srtpProfile = srtp_profile_aead_aes_128_gcm;
                                            srtpKeySize = SRTP_AES_128_KEY_LEN;
                                            srtpSaltSize = SRTP_AEAD_SALT_LEN;
                                            break;
                                        case SRTP_AES128_CM_SHA1_80:
                                            srtpProfile = srtp_profile_aes128_cm_sha1_80;
                                            srtpKeySize = SRTP_AES_128_KEY_LEN;
                                            srtpSaltSize = SRTP_SALT_LEN;
                                            break;
                                        case SRTP_AES128_CM_SHA1_32:
                                            srtpProfile = srtp_profile_aes128_cm_sha1_32;
                                            srtpKeySize = SRTP_AES_128_KEY_LEN;
                                            srtpSaltSize = SRTP_SALT_LEN;
                                            break;
                                        default:
                                            break;
                                    }

                                    if (srtpKeySize == 0) {
                                        LOG("Invalid SRTP profile");
                                        dtlsState = DtlsState::Failed;
                                        setConnectionState(ConnectionState::Failed);
                                    } else {
                                        srtp_create(&srtp_in, nullptr);
                                        srtp_create(&srtp_out, nullptr);

                                        const auto srtpKeyPlusSaltSize = srtpKeySize + srtpSaltSize;
                                        const auto material = ByteBuffer{srtpKeyPlusSaltSize * 2};

                                        std::string label = "EXTRACTOR-dtls_srtp";
                                        SSL_export_keying_material(dtls_ssl,
                                                                   material.data(), srtpKeyPlusSaltSize * 2,
                                                                   label.data(), label.size(),
                                                                   nullptr, 0, 0);

                                        const auto srtpClientKey = material.data();
                                        const auto srtpServerKey = srtpClientKey + srtpKeySize;
                                        const auto srtpClientSalt = srtpServerKey + srtpKeySize;
                                        const auto srtpServerSalt = srtpClientSalt + srtpSaltSize;

                                        srtp_client_key_buf.clear();
                                        srtp_client_key_buf.append(srtpClientKey, srtpKeySize);
                                        srtp_client_key_buf.append(srtpClientSalt, srtpSaltSize);

                                        srtp_server_key_buf.clear();
                                        srtp_server_key_buf.append(srtpServerKey, srtpKeySize);
                                        srtp_server_key_buf.append(srtpServerSalt, srtpSaltSize);

                                        srtp_policy_t srtpReceivePolicy = { };
                                        srtpReceivePolicy.ssrc.type = ssrc_any_inbound;
                                        srtpReceivePolicy.key = answer->isSetupActive() ? srtp_client_key_buf.data() : srtp_server_key_buf.data();
                                        srtpReceivePolicy.allow_repeat_tx = true;

                                        srtp_crypto_policy_set_from_profile_for_rtp(&srtpReceivePolicy.rtp, srtpProfile);
                                        srtp_crypto_policy_set_from_profile_for_rtcp(&srtpReceivePolicy.rtcp, srtpProfile);

                                        srtp_add_stream(srtp_in, &srtpReceivePolicy);

                                        srtp_policy_t srtpSendPolicy = { };
                                        srtpSendPolicy.ssrc.type = ssrc_any_outbound;
                                        srtpSendPolicy.key = answer->isSetupActive() ? srtp_server_key_buf.data() : srtp_client_key_buf.data();
                                        srtpSendPolicy.allow_repeat_tx = true;

                                        srtp_crypto_policy_set_from_profile_for_rtp(&srtpSendPolicy.rtp, srtpProfile);
                                        srtp_crypto_policy_set_from_profile_for_rtcp(&srtpSendPolicy.rtcp, srtpProfile);

                                        srtp_add_stream(srtp_out, &srtpSendPolicy);

                                        dtlsState = DtlsState::Completed;
                                        setConnectionState(ConnectionState::Connected);
                                    }
                                } else {
                                    // Error, certificate hash does not match
                                    LOG("Server cert doesn't match the fingerprint");
                                    dtlsState = DtlsState::Failed;
                                    setConnectionState(ConnectionState::Failed);
                                }
                            }
                        } else {
                            // Error during DTLS handshake
                            LOG("Failed during DTLS handshake");
                            dtlsState = DtlsState::Failed;
                            setConnectionState(ConnectionState::Failed);
                        }
                    }
                }
            } else if (is_rtc_message(data.buf)) {
                LOG("Received RTP/RTCP message size = %zd, id = %d", data.buf.size(), data.buf.data()[0]);

                if (is_rtcp_message(data.buf)) {
                    int rtcpSize = static_cast<int>(data.buf.size());
                    const auto status = srtp_unprotect_rtcp(srtp_in, data.buf.data(), &rtcpSize);
                    LOG("RTCP unprotect: %d, size = %d", status, rtcpSize);

                    if (status == srtp_err_status_ok) {
                        if (rtcpSize >= 8) {
                            ByteReader rtcpReader = { data.buf.data(), static_cast<size_t>(rtcpSize) };

                            const auto rtcpRC = rtcpReader.readU8() & 0x1f;
                            const auto rtcpPT = rtcpReader.readU8();
                            const auto rtcpLength = 4 * (1 + rtcpReader.readU16());
                            const auto rtcpSSRC = rtcpReader.readU32();

                            LOG("RTCP payload = %d, len = %d, SSRC = %u", rtcpPT,
                                rtcpLength, rtcpSSRC);

                            if (rtcpPT == 201) {
                                // https://datatracker.ietf.org/doc/html/rfc3550#section-6.4.2
                                // RR: Receiver Report
                                if (rtcpReader.remaining() >= 4) {
                                    const auto rtcpSSRC_1 = rtcpReader.readU32();
                                    LOG("RTCP RR SSRC = %u", rtcpSSRC_1);
                                }
                            } else if (rtcpPT == 205) {
                                // https://datatracker.ietf.org/doc/html/rfc4585#section-6.2
                                // RTPFB: Transport layer FB message
                                if (rtcpReader.remaining() >= 4) {
                                    const auto rtcpFmt = rtcpRC;
                                    const auto rtcpSSRC_1 = rtcpReader.readU32();
                                    LOG("RTCP RTPFB FMT = %u, SSRC = %u", rtcpFmt, rtcpSSRC_1);

                                    switch (rtcpFmt) {
                                        // https://datatracker.ietf.org/doc/html/rfc4585#section-6.2.1
                                        case 1: {
                                            while (rtcpReader.remaining() >= 4) {
                                                const auto pid = rtcpReader.readU16();
                                                const auto blp = rtcpReader.readU16();

                                                LOG("RTCP RTPFB lost SEQ = %u, blp = 0x%04x", pid, blp);

                                                std::vector<uint16_t> missingSeqList;
                                                missingSeqList.push_back(pid);

                                                if (blp != 0) {
                                                    for (auto index = 0; index < 16; index += 1) {
                                                        if (blp & (1 << index)) {
                                                            LOG("RTCP RTPFB lost SEQ = %u from blp", pid + index + 1);
                                                            missingSeqList.push_back(pid + index + 1);
                                                        }
                                                    }
                                                }

                                                for (const auto seq : missingSeqList) {
                                                    const auto packet = sendHistory->find(rtcpSSRC_1, seq);
                                                    if (packet) {
                                                        // Generate
                                                        auto packetData = packet->generate();

                                                        // Encrypt
                                                        void* rtp_header = packetData.data();
                                                        int rtp_size_1 = static_cast<int>(packetData.size());
                                                        int rtp_size_2 = rtp_size_1;

                                                        packetData.padding(SRTP_MAX_TRAILER_LEN);

                                                        const auto protectStatus = srtp_protect(srtp_out, rtp_header, &rtp_size_2);
                                                        if (protectStatus == srtp_err_status_ok) {
                                                            assert(rtp_size_2 > rtp_size_1);

                                                            // And send
                                                            const auto sentSize = socket->send(rtp_header, rtp_size_2);
                                                            LOG("Re-sent RTP packet with SSRC = %u, SEQ = %u, size = %zu",
                                                                packet->getSSRC(), packet->getSequence(), sentSize);
                                                        } else {
                                                            LOG("Error applying SRTP protection: %d", protectStatus);
                                                        }
                                                    } else {
                                                        LOG("Cannot find packet with SSRC = %u, SEQ = %u for re-sending", rtcpSSRC_1, seq);
                                                    }
                                                }
                                            }
                                        }   break;
                                        default:
                                            LOG("RTCP RTPFB Unknown fmt = %u", rtcpFmt);
                                            break;
                                    }
                                }
                            } else if (rtcpPT == 206) {
                                // https://datatracker.ietf.org/doc/html/rfc4585#section-6.3
                                // PSFB: Payload-specific FB message
                                if (rtcpReader.remaining() >= 4) {
                                    const auto rtcpFmt = rtcpRC;
                                    const auto rtcpSSRC_1 = rtcpReader.readU32();
                                    LOG("RTCP PSFB FMT = %u, SSRC = %u", rtcpFmt, rtcpSSRC_1);

                                    switch (rtcpFmt) {
                                        case 1:
                                            LOG("RTCP PSFB Picture Loss Indication");
                                            break;
                                        default:
                                            LOG("RTCP PSFB Unknown fmt = %u", rtcpFmt);
                                            break;
                                    }
                                }
                            } else if (rtcpPT == 207) {
                                // https://datatracker.ietf.org/doc/html/rfc3611#section-3
                                // XR: Extended Report
                                while (rtcpReader.remaining() >= 4) {
                                    const auto blockType = rtcpReader.readU8();
                                    const auto blockTypeSpecific = rtcpReader.readU8();
                                    const auto blockLength = 4 * (1 + rtcpReader.readU16());

                                    LOG("RTCP XR block type = %d, type specific = %d, len = %u",
                                        blockType, blockTypeSpecific, blockLength);

                                    switch (blockType) {
                                        // https://datatracker.ietf.org/doc/html/rfc3611#section-4.4
                                        case 4: {
                                            LOG("RTCP XR Receiver Reference Time Report Block");
                                            if (blockLength == 4 + 8 && rtcpReader.remaining() >= 8) {
                                                // https://stackoverflow.com/questions/29112071/how-to-convert-ntp-time-to-unix-epoch-time-in-c-language-linux
                                                const auto ntpTimeSeconds = rtcpReader.readU32() - 2208988800U;
                                                // const auto ntpTimeFraction = rtcpReader.readU32();

                                                LOG("RTCP XR Receiver Reference Time = %u", ntpTimeSeconds);
                                            }
                                        }   break;
                                        default:
                                            LOG("RTCP XR unknown block type %d", blockType);
                                            break;
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                LOG("Received unknown message %zd, %d", data.buf.size(), data.buf.data()[0]);
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

            SSL_set_tlsext_use_srtp(dtls_ssl, kSrtpCipherList);
            SSL_set_connect_state(dtls_ssl);

            if (answer->isSetupActive()) {
                SSL_set_accept_state(dtls_ssl);
            } else {
                SSL_set_connect_state(dtls_ssl);
                SSL_do_handshake(dtls_ssl);
            }
        }
    }

    if (srtp_in) {
        srtp_dealloc(srtp_in);
    }
    if (srtp_out) {
        srtp_dealloc(srtp_out);
    }

    if (dtls_ssl) {
        SSL_shutdown(dtls_ssl);
    }

    SSL_free(dtls_ssl);
    SSL_CTX_free(dtls_ctx);

    close(epollHandle);
    epollHandle = -1;

    setConnectionState(ConnectionState::Closed);
}

void PeerConnection::enqueueForSending(ByteBuffer&& buf)
{
    LOG("Enqueing %zd bytes", buf.size());

    std::lock_guard lock(mMutex);
    mRawSendQueue.push_back(std::move(buf));
    eventfd_write(mEventHandle, 1);
}

void PeerConnection::setConnectionState(ConnectionState state)
{
    {
        std::lock_guard lock1(mMutex);
        mConnectionState = state;
    }

    {
        std::lock_guard lock2(mListenerMutex);
        if (mConnectionStateListener) {
            mConnectionStateListener(state);
        }
    }
}


}

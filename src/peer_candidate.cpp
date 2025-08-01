#ifdef _WIN32
#include "srtc/srtc.h"
#include <wincrypt.h>
#undef X509_NAME
#undef X509_EXTENSIONS
#undef PKCS7_SIGNER_INFO
#endif

#include "srtc/event_loop.h"
#include "srtc/ice_agent.h"
#include "srtc/logging.h"
#include "srtc/packetizer.h"
#include "srtc/peer_candidate.h"
#include "srtc/rtcp_packet.h"
#include "srtc/rtcp_packet_source.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_extension_source_simulcast.h"
#include "srtc/rtp_extension_source_twcc.h"
#include "srtc/rtp_responder_twcc.h"
#include "srtc/rtp_std_extensions.h"
#include "srtc/rtp_time_source.h"
#include "srtc/sdp_answer.h"
#include "srtc/sdp_offer.h"
#include "srtc/send_pacer.h"
#include "srtc/send_rtp_history.h"
#include "srtc/sender_report.h"
#include "srtc/sender_reports_history.h"
#include "srtc/srtp_connection.h"
#include "srtc/srtp_openssl.h"
#include "srtc/track.h"
#include "srtc/track_stats.h"
#include "srtc/x509_certificate.h"

#include <cassert>
#include <cstring>

#include <openssl/err.h>
#include <openssl/ssl.h>

#define LOG(level, ...) srtc::log(level, "PeerCandidate", __VA_ARGS__)

namespace
{

std::atomic<uint32_t> gNextUniqueId = 0;

int verify_callback(int ok, X509_STORE_CTX* store_ctx)
{
    // We verify cert has ourselves after the handshake has completed
    return 1;
}

std::string get_openssl_error()
{
    BIO* bio = BIO_new(BIO_s_mem());
    ERR_print_errors(bio);
    char* buf;
    size_t len = BIO_get_mem_data(bio, &buf);
    std::string ret(buf, len);
    BIO_free(bio);
    return ret;
}

constexpr auto kIceMessageBufferSize = 2048;

constexpr auto kConnectTimeout = std::chrono::milliseconds(5000);
constexpr auto kConnectionLostTimeout = std::chrono::milliseconds(5000);
constexpr auto kExpireStunPeriod = std::chrono::milliseconds(1000);
constexpr auto kExpireStunTimeout = std::chrono::milliseconds(5000);
constexpr auto kKeepAliveCheckTimeout = std::chrono::milliseconds(1000);
constexpr auto kKeepAliveSendTimeout = std::chrono::milliseconds(3000);
constexpr auto kConnectRepeatPeriod = std::chrono::milliseconds(100);
constexpr auto kConnectRepeatIncrement = std::chrono::milliseconds(100);
constexpr auto kMaxRecentEnough = std::chrono::milliseconds(5 * 1000);

// https://datatracker.ietf.org/doc/html/rfc5245#section-4.1.2.1
uint32_t make_stun_priority(int type_preference, int local_preference, uint8_t component_id)
{
    return (1 << 24) * type_preference + (1 << 8) * local_preference + (256 - component_id);
}

stun::StunMessage make_stun_message_binding_request(const std::shared_ptr<srtc::IceAgent>& agent,
                                                    uint8_t* buf,
                                                    size_t len,
                                                    const std::shared_ptr<srtc::SdpOffer>& offer,
                                                    const std::shared_ptr<srtc::SdpAnswer>& answer,
                                                    const char* label,
                                                    bool useCandidate)
{
    stun::StunMessage msg = {};
    agent->initRequest(&msg, buf, len, stun::STUN_BINDING);

    if (useCandidate) {
        stun_message_append_flag(&msg, stun::STUN_ATTRIBUTE_USE_CANDIDATE);
    }

    const uint32_t priority = make_stun_priority(200, 10, 1);
    stun::stun_message_append32(&msg, stun::STUN_ATTRIBUTE_PRIORITY, priority);

    // https://datatracker.ietf.org/doc/html/rfc5245#section-7.1.2.3
    const auto offerUserName = offer->getIceUFrag();
    const auto answerUserName = answer->getIceUFrag();
    const auto iceUserName = answerUserName + ":" + offerUserName;
    const auto icePassword = answer->getIcePassword();

    agent->finishMessage(&msg, iceUserName, icePassword);

    return msg;
}

stun::StunMessage make_stun_message_binding_response(const std::shared_ptr<srtc::IceAgent>& agent,
                                                     uint8_t* buf,
                                                     size_t len,
                                                     const std::shared_ptr<srtc::SdpOffer>& offer,
                                                     const std::shared_ptr<srtc::SdpAnswer>& answer,
                                                     const stun::StunMessage& request,
                                                     const srtc::anyaddr& address,
                                                     socklen_t addressLen)
{
    stun::StunMessage msg = {};
    agent->initResponse(&msg, buf, len, &request);

    stun_message_append_xor_addr(&msg, stun::STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS, &address.ss, addressLen);

    // https://datatracker.ietf.org/doc/html/rfc5245#section-7.1.2.3
    const auto icePassword = offer->getIcePassword();

    agent->finishMessage(&msg, std::nullopt, icePassword);

    return msg;
}

bool is_stun_message(const srtc::ByteBuffer& buf)
{
    // https://datatracker.ietf.org/doc/html/rfc5764#section-5.1.2
    if (buf.size() > 20) {
        const auto data = buf.data();
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

bool is_dtls_message(const srtc::ByteBuffer& buf)
{
    // https://datatracker.ietf.org/doc/html/rfc7983#section-5
    if (buf.size() >= 4) {
        const auto data = buf.data();
        if (data[0] >= 20 && data[0] <= 24) {
            return true;
        }
    }

    return false;
}

bool is_rtc_message(const srtc::ByteBuffer& buf)
{
    // https://datatracker.ietf.org/doc/html/rfc3550#section-5.1
    if (buf.size() >= 8) {
        const auto data = buf.data();
        if (data[0] >= 128 && data[0] <= 191) {
            return true;
        }
    }

    return false;
}

bool is_rtcp_message(const srtc::ByteBuffer& buf)
{
    // https://datatracker.ietf.org/doc/html/rfc5761#section-4
    if (buf.size() >= 8) {
        const auto data = buf.data();
        const auto payloadId = data[1] & 0x7F;
        return payloadId >= 64 && payloadId <= 95;
    }
    return false;
}

uint8_t findVideoExtension(const std::shared_ptr<srtc::SdpAnswer>& answer, const std::string& name)
{
    return answer->getVideoExtensionMap().findByName(name);
}

float calculateLayerBandwidthScale(const std::vector<std::shared_ptr<srtc::SimulcastLayer>>& layerList,
                                   const std::shared_ptr<srtc::SimulcastLayer>& trackLayer)
{
    if (layerList.empty()) {
        return 1.0f;
    }

    uint32_t total = 0;
    for (const auto& layer : layerList) {
        total += layer->kilobits_per_second;
    }

    return static_cast<float>(trackLayer->kilobits_per_second) / static_cast<float>(total);
}

} // namespace

namespace srtc
{

PeerCandidate::PeerCandidate(PeerCandidateListener* const listener,
                             const std::vector<std::shared_ptr<Track>>& trackList,
                             const std::shared_ptr<SdpOffer>& offer,
                             const std::shared_ptr<SdpAnswer>& answer,
                             const std::shared_ptr<RealScheduler>& scheduler,
                             const Host& host,
                             const std::shared_ptr<EventLoop>& eventLoop,
                             const Scheduler::Delay& startDelay)
    : mListener(listener)
    , mTrackList(trackList)
    , mOffer(offer)
    , mAnswer(answer)
    , mHost(host)
    , mEventLoop(eventLoop)
    , mSocket(std::make_shared<Socket>(host.addr))
    , mIceAgent(std::make_shared<IceAgent>())
    , mIceMessageBuffer(std::make_unique<uint8_t[]>(kIceMessageBufferSize))
    , mSendRtpHistory(std::make_shared<SendRtpHistory>())
    , mUniqueId(++gNextUniqueId)
    , mVideoExtMediaId(findVideoExtension(answer, RtpStandardExtensions::kExtSdesMid))
    , mVideoExtStreamId(findVideoExtension(answer, RtpStandardExtensions::kExtSdesRtpStreamId))
    , mVideoExtRepairedStreamId(findVideoExtension(answer, RtpStandardExtensions::kExtSdesRtpRepairedStreamId))
    , mVideoExtGoogleVLA(findVideoExtension(answer, RtpStandardExtensions::kExtGoogleVLA))
    , mExtensionSourceSimulcast(RtpExtensionSourceSimulcast::factory(answer->isVideoSimulcast(),
                                                                     mVideoExtMediaId,
                                                                     mVideoExtStreamId,
                                                                     mVideoExtRepairedStreamId,
                                                                     mVideoExtGoogleVLA))
    , mExtensionSourceTWCC(RtpExtensionSourceTWCC::factory(offer, answer, scheduler))
    , mResponderTWCC(RtpResponderTWCC::factory(offer, answer))
    , mSenderReportsHistory(std::make_shared<SenderReportsHistory>())
    , mIceRttFilter(0.2f)
    , mControlRttFilter(0.2f)
    , mSentUseCandidate(false)
    , mIsConnected(false)
    , mLastSendTime(std::chrono::steady_clock::time_point::min())
    , mLastReceiveTime(std::chrono::steady_clock::time_point::min())
    , mScheduler(scheduler)
{
    assert(mListener);

    LOG(SRTC_LOG_V,
        "Constructor for %p #%d, host = %s, delay = %ld ms",
        static_cast<void*>(this),
        mUniqueId,
        to_string(host.addr).c_str(),
        static_cast<long>(startDelay.count()));

    initOpenSSL();

    mEventLoop->registerSocket(mSocket, this);

    mScheduler.submit(startDelay, __FILE__, __LINE__, [this] { startConnecting(); });

    // Trim stun requests from time to time
    Task::cancelHelper(mTaskExpireStunRequests);

    mTaskExpireStunRequests =
        mScheduler.submit(kExpireStunPeriod, __FILE__, __LINE__, [this] { forgetExpiredStunRequests(); });
}

PeerCandidate::~PeerCandidate()
{
    LOG(SRTC_LOG_V, "Destructor for %p #%d", static_cast<void*>(this), mUniqueId);

    mEventLoop->unregisterSocket(mSocket);

    freeDTLS();
}

void PeerCandidate::receiveFromSocket()
{
    auto list = mSocket->receive();
    for (auto& item : list) {
        mRawReceiveQueue.push_back(std::move(item));
    }
    list.clear();
}

void PeerCandidate::addSendFrame(PeerCandidate::FrameToSend&& frame)
{
    mFrameSendQueue.push_back(std::move(frame));
}

void PeerCandidate::sendSenderReports(const std::vector<std::shared_ptr<Track>>& trackList)
{
    for (const auto& track : trackList) {
        if (track->getDirection() == Direction::Publish) {
            const auto ssrc = track->getSSRC();

            ByteBuffer payload;
            ByteWriter w(payload);

            // https://www4.cs.fau.de/Projects/JRTP/pmt/node83.html

            NtpTime ntp = {};
            getNtpTime(ntp);

            const auto timeSource = track->getRtpTimeSource();
            const auto rtpTime = timeSource->getCurrTimestamp();

            w.writeU32(ntp.seconds);
            w.writeU32(ntp.fraction);
            w.writeU32(rtpTime);

            const auto stats = track->getStats();
            w.writeU32(stats->getSentPackets());
            w.writeU32(stats->getSentBytes());

            const auto packet = std::make_shared<RtcpPacket>(ssrc, 0, RtcpPacket::kSenderReport, std::move(payload));
            sendRtcpPacket(track, packet);

            mSenderReportsHistory->save(ssrc, ntp);
        }
    }
}

void PeerCandidate::sendReceiverReports(const std::vector<std::shared_ptr<Track>>& trackList)
{
    for (const auto& track : trackList) {
        const auto direction = track->getDirection();
        if (direction == Direction::Subscribe || (direction == Direction::Publish && track->getRemoteSSRC() != 0)) {
            const auto ssrc = direction == Direction::Subscribe ? track->getSSRC() : track->getRemoteSSRC();
            const auto stats = track->getStats();

            const auto seq = stats->getReceivedHighestSeqEx();
            const auto sr = stats->getReceivedSenderReport();

            ByteBuffer payload;
            ByteWriter w(payload);

            w.writeU32(ssrc);
            w.writeU32(0); // TODO: fraction lost | cumulative number of packets lost
            w.writeU32(static_cast<uint32_t>(seq));
            w.writeU32(0); // TODO: interarrival jitter

            if (sr.has_value()) {
                const auto sr_value = sr.value();
                const auto lastSRMiddle32 = static_cast<uint32_t>(sr_value.ntp.seconds & 0xFFFFu) << 16 ||
                                            static_cast<uint32_t>(sr_value.ntp.fraction >> 16) % 0xFFFFu;
                const auto lastSRDelayDelta = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - sr_value.when);
                const auto lastSRDelayValue = lastSRDelayDelta.count() * static_cast<int64_t>(65536) / 1000;

                w.writeU32(lastSRMiddle32);
                w.writeU32(lastSRDelayValue);
            } else {
                w.writeU32(0);
                w.writeU32(0);
            }

            const auto packet = std::make_shared<RtcpPacket>(0, 0, RtcpPacket::kReceiverReport, std::move(payload));
            sendRtcpPacket(track, packet);
        }
    }
}

void PeerCandidate::sendPictureLossIndicators(const std::vector<std::shared_ptr<Track>>& trackList)
{
    for (const auto& track : trackList) {
        if (track->getMediaType() == MediaType::Video && track->getDirection() == Direction::Subscribe) {
            const auto ssrc = track->getSSRC();

            LOG(SRTC_LOG_V, "Sending PLI for ssrc = %u", ssrc);

            ByteBuffer payload;
            ByteWriter w(payload);

            w.writeU32(ssrc);

            const auto packet = std::make_shared<RtcpPacket>(0, 1, RtcpPacket::kPayloadSpecific, std::move(payload));
            sendRtcpPacket(track, packet);
        }
    }
}

void PeerCandidate::sendNacks(const std::shared_ptr<Track>& track, const std::vector<uint16_t>& nackList)
{
    if (nackList.empty()) {
        return;
    }
    const auto nackSize = nackList.size();

    const auto seqList = std::make_unique<uint16_t[]>(nackSize);
    const auto blpList = std::make_unique<uint16_t[]>(nackSize);

    const auto n = compressNackList(nackList, seqList.get(), blpList.get());

    for (size_t i = 0; i < n; i += 1) {
        ByteBuffer payload;
        ByteWriter w(payload);

        const auto ssrc = track->getSSRC();
        const auto seq = seqList[i];
        const auto blp = blpList[i];

        w.writeU32(ssrc);
        w.writeU16(seq);
        w.writeU16(blp);

        LOG(SRTC_LOG_V,
            "Sending NACK for media %s, SSRC = %u, SEQ = %u, BLP = 0x%x",
            to_string(track->getMediaType()).c_str(),
            ssrc,
            seq,
            blp);

        const auto packet = std::make_shared<RtcpPacket>(0, 1, RtcpPacket::kFeedback, std::move(payload));
        sendRtcpPacket(track, packet);
    }
}

void PeerCandidate::updatePublishConnectionStats(PublishConnectionStats& stats) const
{
    const auto now = std::chrono::steady_clock::now();
    if (now - mControlRttFilter.getWhenUpdated() <= kMaxRecentEnough) {
        // RTT from sender / receiver reports
        stats.rtt_ms = mControlRttFilter.value();
    } else if (now - mIceRttFilter.getWhenUpdated() <= kMaxRecentEnough) {
        // RTT from STUN requests / responses
        stats.rtt_ms = mIceRttFilter.value();
    }

    if (mExtensionSourceTWCC) {
        mExtensionSourceTWCC->updatePublishConnectionStats(stats);
    }
}

std::optional<float> PeerCandidate::getIceRtt() const
{
    // RTT from STUN requests / responses
    return mIceRttFilter.value();
}

[[nodiscard]] int PeerCandidate::getTimeoutMillis(int defaultValue) const
{
    if (mSendPacer) {
        return mSendPacer->getTimeoutMillis(defaultValue);
    }
    return defaultValue;
}

void PeerCandidate::run()
{
    // Sending
    if (mSendPacer) {
        mSendPacer->run();
    }

    // Raw data
    while (!mRawSendQueue.empty()) {
        const auto buf = std::move(mRawSendQueue.front());
        mRawSendQueue.erase(mRawSendQueue.begin());

        const auto w = mSocket->send(buf);
        LOG(SRTC_LOG_V, "Sent %zd raw bytes", w);
    }

    // Frames
    while (!mFrameSendQueue.empty()) {
        const auto item = std::move(mFrameSendQueue.front());
        mFrameSendQueue.erase(mFrameSendQueue.begin());

        if (mSrtpConnection == nullptr || mSendPacer == nullptr) {
            // We are not connected yet
            LOG(SRTC_LOG_E, "We are not connected yet, not sending a frame");
            continue;
        }

        if (!item.csd.empty()) {
            // Codec Specific Data
            item.packetizer->setCodecSpecificData(item.csd);
        } else if (!item.buf.empty()) {
            // Simulcast layers
            std::vector<std::shared_ptr<SimulcastLayer>> layerList;
            if (item.track->isSimulcast()) {
                for (const auto& trackItem : mAnswer->getVideoSimulcastTrackList()) {
                    layerList.push_back(trackItem->getSimulcastLayer());
                }
            }

            // Frame data
            if (mExtensionSourceSimulcast) {
                if (item.track->isSimulcast() &&
                    mExtensionSourceSimulcast->shouldAdd(item.track, item.packetizer, item.buf)) {
                    mExtensionSourceSimulcast->prepare(item.track, layerList);
                } else {
                    mExtensionSourceSimulcast->clear();
                }
            }

            // Packetize
            const auto packetList = item.packetizer->generate(mExtensionSourceSimulcast,
                                                              mExtensionSourceTWCC,
                                                              mSrtpConnection->getMediaProtectionOverhead(),
                                                              item.buf);

            // Flush any packets from the same track which we haven't sent yet
            mSendPacer->flush(item.track);

            // Use the pacer to send
            if (!packetList.empty()) {
                if (packetList.size() <= 1) {
                    mSendPacer->sendNow(packetList.front());
                } else {
                    auto spread = SendPacer::kDefaultSpreadMillis;
                    if (mExtensionSourceTWCC) {
                        auto bandwidthScale = 1.0f;
                        if (item.track->isSimulcast()) {
                            // Each layer gets a portion of the total bandwidth
                            bandwidthScale = calculateLayerBandwidthScale(layerList, item.track->getSimulcastLayer());
                        }
                        spread = mExtensionSourceTWCC->getPacingSpreadMillis(packetList, bandwidthScale, spread);
                    }
                    mSendPacer->sendPaced(packetList, spread);
                }
            }
        }
    }

    // Receive
    while (!mRawReceiveQueue.empty()) {
        Socket::ReceivedData data = std::move(mRawReceiveQueue.front());
        mRawReceiveQueue.erase(mRawReceiveQueue.begin());

        if (is_stun_message(data.buf)) {
            LOG(SRTC_LOG_V, "Received STUN message %zd, %d, #%u", data.buf.size(), data.buf.front(), mUniqueId);
            onReceivedStunMessage(data);
        } else if (mDtlsSsl && is_dtls_message(data.buf)) {
            LOG(SRTC_LOG_V, "Received DTLS message %zd, %d, #%u", data.buf.size(), data.buf.front(), mUniqueId);
            onReceivedDtlsMessage(std::move(data.buf));
        } else if (is_rtc_message(data.buf)) {
            LOG(SRTC_LOG_V, "Received RTP/RTCP message size = %zd, id = %d", data.buf.size(), data.buf.front());
            onReceivedRtcMessage(std::move(data.buf));
        } else {
            LOG(SRTC_LOG_V, "Received unknown message %zd, %d", data.buf.size(), data.buf.front());
        }
    }

    if (mDtlsState == DtlsState::Activating && mDtlsSsl == nullptr) {
        LOG(SRTC_LOG_V, "Preparing for the DTLS handshake");

        const auto cert = mOffer->getCertificate();
        mDtlsCtx = SSL_CTX_new(mAnswer->isSetupActive() ? DTLS_server_method() : DTLS_client_method());

        SSL_CTX_use_certificate(mDtlsCtx, cert->getCertificate());
        SSL_CTX_use_PrivateKey(mDtlsCtx, cert->getPrivateKey());

        if (!SSL_CTX_check_private_key(mDtlsCtx)) {
            LOG(SRTC_LOG_V, "ERROR: invalid private key");
            mDtlsState = DtlsState::Failed;
            emitOnFailedToConnect({ Error::Code::InvalidData, "Invalid private key" });
        } else {
            SSL_CTX_set_verify(mDtlsCtx, SSL_VERIFY_PEER, verify_callback);

            SSL_CTX_set_min_proto_version(mDtlsCtx, DTLS1_VERSION);
            SSL_CTX_set_max_proto_version(mDtlsCtx, DTLS1_2_VERSION);
            SSL_CTX_set_read_ahead(mDtlsCtx, 1);

            mDtlsSsl = SSL_new(mDtlsCtx);

            mDtlsBio = BIO_new_dgram(this);
            SSL_set_bio(mDtlsSsl, mDtlsBio, mDtlsBio);

            SSL_set_tlsext_use_srtp(mDtlsSsl, SrtpConnection::kSrtpCipherList);
            SSL_set_connect_state(mDtlsSsl);

            if (mAnswer->isSetupActive()) {
                SSL_set_accept_state(mDtlsSsl);
                SSL_accept(mDtlsSsl);
            } else {
                SSL_set_connect_state(mDtlsSsl);
                SSL_do_handshake(mDtlsSsl);
            }
        }
    }

    // TWCC
    if (mResponderTWCC) {
        const auto track = mTrackList.front();
        const auto list = mResponderTWCC->run(track);
        if (!list.empty()) {
            for (const auto& packet : list) {
                sendRtcpPacket(track, packet);
            }
        }
    }
}

void PeerCandidate::startConnecting()
{
    // Notify the listener
    emitOnConnecting();

    // Clean up some things
    Task::cancelHelper(mTaskConnectionLostTimeout);
    Task::cancelHelper(mTaskKeepAliveTimeout);

    mSrtpConnection.reset();
    mSendPacer.reset();

    // Connecting should take a limited amount of time
    Task::cancelHelper(mTaskConnectTimeout);

    mTaskConnectTimeout = mScheduler.submit(kConnectTimeout, __FILE__, __LINE__, [this] {
        emitOnFailedToConnect({ Error::Code::InvalidData, "Connect timeout" });
    });

    // Open the conversation by sending a STUN binding request
    sendStunBindingRequest(0);
}

void PeerCandidate::addSendRaw(ByteBuffer&& buf)
{
    mRawSendQueue.push_back(std::move(buf));
    mListener->onCandidateHasDataToSend(this);
}

void PeerCandidate::onReceivedStunMessage(const Socket::ReceivedData& data)
{
    stun::StunMessage incomingMessage = {};
    incomingMessage.buffer = const_cast<uint8_t*>(data.buf.data());
    incomingMessage.buffer_len = data.buf.size();

    const auto stunMessageClass = stun::stun_message_get_class(&incomingMessage);
    const auto stunMessageMethod = stun::stun_message_get_method(&incomingMessage);

    LOG(SRTC_LOG_V, "Received STUN message class  = %d", stunMessageClass);
    LOG(SRTC_LOG_V, "Received STUN message method = %d", stunMessageMethod);

    if (stunMessageClass == stun::STUN_REQUEST && stunMessageMethod == stun::STUN_BINDING) {
        const auto offerUserName = mOffer->getIceUFrag();
        const auto answerUserName = mAnswer->getIceUFrag();
        const auto iceUserName = offerUserName + ":" + answerUserName;
        const auto icePassword = mOffer->getIcePassword();

        if (mIceAgent->verifyRequestMessage(&incomingMessage, iceUserName, icePassword)) {
            const auto response = make_stun_message_binding_response(mIceAgent,
                                                                     mIceMessageBuffer.get(),
                                                                     kIceMessageBufferSize,
                                                                     mOffer,
                                                                     mAnswer,
                                                                     incomingMessage,
                                                                     data.addr,
                                                                     data.addr_len);
            addSendRaw({ mIceMessageBuffer.get(), stun::stun_message_length(&response) });
        } else {
            LOG(SRTC_LOG_E, "STUN request verification failed, ignoring");
        }
    } else if (stunMessageClass == stun::STUN_RESPONSE && stunMessageMethod == stun::STUN_BINDING) {
        int errorCode = { 0 };
        if (stun::stun_message_find_error(&incomingMessage, &errorCode) == stun::STUN_MESSAGE_RETURN_SUCCESS) {
            LOG(SRTC_LOG_V, "STUN response error code: %d", errorCode);
        }

        uint8_t id[STUN_MESSAGE_TRANS_ID_LEN];
        stun::stun_message_id(&incomingMessage, id);

        float rtt = 0.0f;
        if (mIceAgent->forgetTransaction(id, rtt)) {
            LOG(SRTC_LOG_V, "Removed old STUN transaction ID for binding request, rtt = %.2f", rtt);

            mIceRttFilter.update(rtt);

            if (errorCode == 0 && mIceAgent->verifyResponseMessage(&incomingMessage, mAnswer->getIcePassword())) {
                if (mSentUseCandidate) {
                    // Keep-alive
                    LOG(SRTC_LOG_V, "STUN keep-alive response verification succeeded");

                    if (mDtlsState == DtlsState::Completed) {
                        // We are connected again
                        onReceivedFromRemote();
                    }
                } else {
                    // Initial connection
                    LOG(SRTC_LOG_V, "STUN binding response verification succeeded, sending use candidate request");

                    mSentUseCandidate = true;

                    emitOnIceSelected();
                    sendStunBindingResponse(0);

                    mDtlsState = DtlsState::Activating;
                }
            } else {
                LOG(SRTC_LOG_E, "STUN response verification failed, ignoring");
            }
        }
    }
}

void PeerCandidate::onReceivedDtlsMessage(ByteBuffer&& buf)
{
    Task::cancelHelper(mTaskSendStunConnectRequest);
    Task::cancelHelper(mTaskSendStunConnectResponse);

    mDtlsReceiveQueue.push_back(std::move(buf));

    // Try the handshake
    if (mDtlsState == DtlsState::Activating) {
        const auto r1 = SSL_do_handshake(mDtlsSsl);
        const auto err = SSL_get_error(mDtlsSsl, r1);
        LOG(SRTC_LOG_V, "DTLS handshake: %d, %d", r1, err);

        if (err == SSL_ERROR_WANT_READ) {
            LOG(SRTC_LOG_V, "Still in progress");
        } else if (r1 == 1 && err == 0) {
            const auto cert = SSL_get_peer_certificate(mDtlsSsl);
            if (cert == nullptr) {
                // Error, no certificate
                LOG(SRTC_LOG_E, "There is no DTLS server certificate");
                mDtlsState = DtlsState::Failed;
                emitOnFailedToConnect({ Error::Code::InvalidData, "There is no DTLS server certificate" });
            } else {
                uint8_t fpBuf[32] = {};
                unsigned int fpSize = {};

                const auto digest = EVP_get_digestbyname("sha256");
                X509_digest(cert, digest, fpBuf, &fpSize);

                std::string hex = bin_to_hex(fpBuf, fpSize);
                LOG(SRTC_LOG_V, "Remote certificate sha-256: %s", hex.c_str());

                const auto expectedHash = mAnswer->getCertificateHash();
                const auto actualHashBin = ByteBuffer{ fpBuf, fpSize };

                if (expectedHash.getBin() == actualHashBin) {
                    const auto [srtpConnection, srtpError] = SrtpConnection::create(mDtlsSsl, mAnswer->isSetupActive());

                    if (srtpError.isOk()) {
                        mSrtpConnection = srtpConnection;
                        mSendPacer =
                            std::make_shared<SendPacer>(mOffer->getConfig(),
                                                        mSrtpConnection,
                                                        mSocket,
                                                        mSendRtpHistory,
                                                        mExtensionSourceTWCC,
                                                        [this]() { mLastSendTime = std::chrono::steady_clock::now(); });
                        mDtlsState = DtlsState::Completed;

                        const auto addr = to_string(mHost.addr);
                        const auto cipher = SSL_get_cipher(mDtlsSsl);
                        const auto profile = SSL_get_selected_srtp_profile(mDtlsSsl);
                        LOG(SRTC_LOG_V,
                            "Connected to %s with cipher %s, profile %s, ice rtt = %.2f ms",
                            addr.c_str(),
                            cipher,
                            profile->name,
                            mIceRttFilter.value());

                        onReceivedFromRemote();
                    } else {
                        // Error, failed to initialize SRTP
                        LOG(SRTC_LOG_E,
                            "Failed to initialize SRTP: %d, %s",
                            static_cast<int>(srtpError.mCode),
                            srtpError.mMessage.c_str());
                        mDtlsState = DtlsState::Failed;

                        Task::cancelHelper(mTaskConnectTimeout);
                        emitOnFailedToConnect(srtpError);
                    }
                } else {
                    // Error, certificate hash does not match
                    LOG(SRTC_LOG_E, "Server cert doesn't match the fingerprint");
                    mDtlsState = DtlsState::Failed;

                    Task::cancelHelper(mTaskConnectTimeout);
                    emitOnFailedToConnect({ Error::Code::InvalidData, "Certificate hash doesn't match" });
                }
            }
        } else {
            // Error during DTLS handshake
            const auto opensslError = get_openssl_error();
            LOG(SRTC_LOG_E, "Failed during DTLS handshake: %s", opensslError.c_str());
            mDtlsState = DtlsState::Failed;

            Task::cancelHelper(mTaskConnectTimeout);
            emitOnFailedToConnect({ Error::Code::InvalidData, "Failure during DTLS handshake: " + opensslError });
        }

        if (mDtlsState == DtlsState::Failed) {
            freeDTLS();
        }
    }
}

void PeerCandidate::onReceivedRtcMessage(ByteBuffer&& buf)
{
    ByteBuffer output;

    if (mSrtpConnection) {
        if (is_rtcp_message(buf)) {
            if (mSrtpConnection->unprotectReceiveControl(buf, output)) {
                LOG(SRTC_LOG_V, "RTCP unprotect: size = %zd", output.size());

                const auto list = RtcpPacket::fromUdpPacket(output);
                for (const auto& packet : list) {
                    onReceivedControlPacket(packet);
                }
            }
        } else {
            if (mSrtpConnection->unprotectReceiveMedia(buf, output)) {
                LOG(SRTC_LOG_V, "RTP unprotect: size = %zd", output.size());

                if (const auto track = findTrack(output)) {
                    const auto packet = RtpPacket::fromUdpPacket(track, output);
                    if (packet) {
                        const auto stats = track->getStats();

                        stats->incrementReceivedPackets(1);
                        stats->incrementReceivedBytes(buf.size());

                        onReceivedMediaPacket(packet);
                    }
                }
            }
        }
    }
}

void PeerCandidate::onReceivedControlPacket(const std::shared_ptr<RtcpPacket>& packet)
{
    onReceivedFromRemote();

    const auto rtcpRC = packet->getRC();
    const auto rtcpPT = packet->getPayloadId();

    const auto& payload = packet->getPayload();
    ByteReader rtcpReader = { payload.data(), payload.size() };

    LOG(SRTC_LOG_V, "RTCP payload = %d, len = %zu, SSRC = %u", rtcpPT, payload.size(), packet->getSSRC());

    if (rtcpPT == 200) {
        // https://datatracker.ietf.org/doc/html/rfc3550#section-6.4.1
        // Sender Report
        onReceivedControlMessage_200(packet->getSSRC(), rtcpReader);
    } else if (rtcpPT == 201) {
        // https://datatracker.ietf.org/doc/html/rfc3550#section-6.4.2
        // Receiver Report
        onReceivedControlMessage_201(rtcpReader);
    } else if (rtcpPT == 205) {
        // https://datatracker.ietf.org/doc/html/rfc4585#section-6.2
        // RTPFB: Transport layer FB message
        if (rtcpReader.remaining() >= 4) {
            const auto rtcpFmt = rtcpRC;
            const auto rtcpSSRC_1 = rtcpReader.readU32();

            LOG(SRTC_LOG_V, "RTCP RTPFB FMT = %u, SSRC = %u", rtcpFmt, rtcpSSRC_1);

            switch (rtcpFmt) {
            case 1:
                // https://datatracker.ietf.org/doc/html/rfc4585#section-6.2.1
                // NACKs
                onReceivedControlMessage_205_1(rtcpSSRC_1, rtcpReader);
                break;
            case 15:
                // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01
                // Google'e Transport-Wide Congension Control
                onReceivedControlMessage_205_15(rtcpSSRC_1, rtcpReader);
                break;
            default:
                LOG(SRTC_LOG_V, "RTCP RTPFB Unknown fmt = %u", rtcpFmt);
                break;
            }
        }
    }
}

void PeerCandidate::onReceivedMediaPacket(const std::shared_ptr<RtpPacket>& packet)
{
    onReceivedFromRemote();

    const auto track = packet->getTrack();

    LOG(SRTC_LOG_V,
        "RTP media packet: media = %s, ssrc = %12" PRIu32 ", seq = %5u, pt = %u, size = %zu",
        to_string(track->getMediaType()).c_str(),
        packet->getSSRC(),
        packet->getSequence(),
        packet->getPayloadId(),
        packet->getPayloadSize());

    if (mResponderTWCC) {
        mResponderTWCC->onMediaPacket(packet);
    }

    mListener->onCandidateReceivedMediaPacket(this, packet);
}

void PeerCandidate::onReceivedControlMessage_200(uint32_t ssrc, srtc::ByteReader& rtcpReader)
{
    const auto track = findTrack(ssrc);

    if (track) {
        if (rtcpReader.remaining() >= 20) {
            const auto ntp_high = rtcpReader.readU32();
            const auto ntp_low = rtcpReader.readU32();
            const auto rtp_timestamp = rtcpReader.readU32();
            const auto packet_count = rtcpReader.readU32();
            const auto octet_count = rtcpReader.readU32();

            LOG(SRTC_LOG_V,
                "*** Sender Report: ssrc = %u, ntp_h = %u, ntp_l = %u, packet_count = %u, octet_count = %u, media = %s",
                ssrc,
                ntp_high,
                ntp_low,
                packet_count,
                octet_count,
                to_string(track->getMediaType()).c_str());

            SenderReport sr;
            sr.when = std::chrono::steady_clock::now();
            sr.ntp.seconds = ntp_high;
            sr.ntp.fraction = ntp_low;
            sr.rtp = rtp_timestamp;
            sr.packet_count = packet_count;
            sr.octet_count = octet_count;

            const auto stats = track->getStats();
            stats->setReceivedSenderReport(sr);

            mListener->onCandidateReceivedSenderReport(this, track, sr);
        }
    } else {
        LOG(SRTC_LOG_W, "Cannot find track with ssrc = %u for a sender report", ssrc);
    }
}

void PeerCandidate::onReceivedControlMessage_201(srtc::ByteReader& rtcpReader)
{
    while (rtcpReader.remaining() >= 24) {
        const auto ssrc = rtcpReader.readU32();
        const auto lost = rtcpReader.readU32();
        const auto highestReceived = rtcpReader.readU32();
        const auto jitter = rtcpReader.readU32();
        const auto lastSR = rtcpReader.readU32();
        const auto delaySinceLastSR = rtcpReader.readU32();

        (void)lost;
        (void)highestReceived;
        (void)jitter;

        const auto rtt = mSenderReportsHistory->calculateRtt(ssrc, lastSR, delaySinceLastSR);
        if (rtt) {
            LOG(SRTC_LOG_V, "RTT from receiver report: %.2f", rtt.value());
            mControlRttFilter.update(rtt.value());
        }
    }
}

void PeerCandidate::onReceivedControlMessage_205_1(uint32_t ssrc, ByteReader& rtcpReader)
{
    while (rtcpReader.remaining() >= 4) {
        const auto pid = rtcpReader.readU16();
        const auto blp = rtcpReader.readU16();

        LOG(SRTC_LOG_V, "RTCP RTPFB lost SEQ = %u, blp = 0x%04x", pid, blp);

        std::vector<uint16_t> missingSeqList;
        missingSeqList.push_back(pid);

        if (blp != 0) {
            for (auto index = 0; index < 16; index += 1) {
                if (blp & (1 << index)) {
                    LOG(SRTC_LOG_V, "RTCP RTPFB lost SEQ = %u from blp", pid + index + 1);
                    missingSeqList.push_back(pid + index + 1);
                }
            }
        }

        for (const auto seq : missingSeqList) {
            const auto packet = mSendRtpHistory->find(ssrc, seq);

            // Record in TWCC
            if (packet && mExtensionSourceTWCC) {
                mExtensionSourceTWCC->onPacketWasNacked(packet);
            }

            if (packet && mSrtpConnection) {
                // Generate
                const auto track = packet->getTrack();

                RtpPacket::Output packetData;
                if (track->getRtxPayloadId() > 0) {
                    RtpExtension extension = packet->getExtension().copy();

                    if (track->isSimulcast() && mExtensionSourceSimulcast) {
                        auto builder = RtpExtensionBuilder::from(extension);
                        mExtensionSourceSimulcast->updateForRtx(builder, track);
                        extension = builder.build();
                    }

                    packetData = packet->generateRtx(extension);
                } else {
                    packetData = packet->generate();
                }

                if (mSrtpConnection->protectSendMedia(packetData.buf, packetData.rollover, mProtectedBuf)) {
                    // And send
                    const auto sentSize = mSocket->send(mProtectedBuf.data(), mProtectedBuf.size());
                    LOG(SRTC_LOG_V,
                        "Re-sent RTP packet with SSRC = %u, SEQ = %u, size = %zu, rtx = %d",
                        packet->getSSRC(),
                        packet->getSequence(),
                        sentSize,
                        packet->getTrack()->getRtxPayloadId() > 0);

                    // Keep stats
                    const auto stats = packet->getTrack()->getStats();
                    stats->incrementSentPackets(1);
                    stats->incrementSentBytes(mProtectedBuf.size());
                } else {
                    LOG(SRTC_LOG_E, "Error protecting packet for re-sending");
                }
            } else {
                LOG(SRTC_LOG_V, "Cannot find packet with SSRC = %u, SEQ = %u for re-sending", ssrc, seq);
            }
        }
    }
}

void PeerCandidate::onReceivedControlMessage_205_15(uint32_t ssrc, ByteReader& rtcpReader)
{
    if (mExtensionSourceTWCC) {
        mExtensionSourceTWCC->onReceivedRtcpPacket(ssrc, rtcpReader);
    }
}

void PeerCandidate::forgetExpiredStunRequests()
{
    mIceAgent->forgetExpiredTransactions(kExpireStunTimeout);

    mTaskExpireStunRequests =
        mScheduler.submit(kExpireStunPeriod, __FILE__, __LINE__, [this] { forgetExpiredStunRequests(); });
}

void PeerCandidate::sendRtcpPacket(const std::shared_ptr<Track>& track, const std::shared_ptr<RtcpPacket>& packet)
{
    if (mSrtpConnection) {
        const auto rtcpSource = track->getRtcpPacketSource();

        const auto packetData = packet->generate();

        if (mSrtpConnection->protectSendControl(packetData, rtcpSource->getNextSequence(), mProtectedBuf)) {
            const auto w = mSocket->send(mProtectedBuf.data(), mProtectedBuf.size());
            LOG(SRTC_LOG_V, "Sent %zu bytes of RTCP", w);
        }
    }
}

std::shared_ptr<Track> PeerCandidate::findTrack(uint32_t ssrc)
{
    for (const auto& track : mTrackList) {
        if (track->getSSRC() == ssrc) {
            return track;
        }
        if (track->getDirection() == Direction::Publish && track->getRemoteSSRC() == ssrc) {
            return track;
        }
    }

    return {};
}

std::shared_ptr<Track> PeerCandidate::findTrack(ByteBuffer& packet)
{
    if (packet.size() < 12) {
        return {};
    }

    const auto ssrc = ntohl(*reinterpret_cast<const uint32_t*>(packet.data() + 8));
    const auto pt = ntohs(*reinterpret_cast<const uint16_t*>(packet.data())) & 0x7Fu;

    for (const auto& track : mTrackList) {
        if (track->getSSRC() == ssrc && track->getPayloadId() == pt) {
            return track;
        }

        if (track->getRtxSSRC() == ssrc && track->getRtxPayloadId() == pt) {
            return track;
        }
    }

    return {};
}

// Custom BIO for DGRAM

struct dgram_data {
    PeerCandidate* pc;
};

int PeerCandidate::dgram_read(BIO* b, char* out, int outl)
{
    if (out == nullptr) {
        return 0;
    }

    BIO_clear_retry_flags(b);

    auto ptr = BIO_get_data(b);
    auto data = reinterpret_cast<dgram_data*>(ptr);

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

int PeerCandidate::dgram_write(BIO* b, const char* in, int inl)
{
    if (inl == 0) {
        return 0;
    }

    auto ptr = BIO_get_data(b);
    auto data = reinterpret_cast<dgram_data*>(ptr);

    data->pc->addSendRaw({ reinterpret_cast<const uint8_t*>(in), static_cast<size_t>(inl) });

    return inl;
}

long PeerCandidate::dgram_ctrl(BIO* b, int cmd, long num, void* ptr)
{
    switch (cmd) {
    case BIO_CTRL_DGRAM_QUERY_MTU:
        return 1200;
    case BIO_CTRL_DUP:
    case BIO_CTRL_FLUSH:
        return 1;
#ifdef BIO_CTRL_GET_KTLS_SEND
    case BIO_CTRL_GET_KTLS_SEND:
        // Fallthrough
#endif
#ifdef BIO_CTRL_GET_KTLS_RECV
    case BIO_CTRL_GET_KTLS_RECV:
        // Fallthrough
#endif
    default:
        return 0;
    }
}

int PeerCandidate::dgram_free(BIO* b)
{
    auto ptr = BIO_get_data(b);
    auto data = reinterpret_cast<dgram_data*>(ptr);
    delete data;
    return 1;
}

std::once_flag PeerCandidate::dgram_once;
BIO_METHOD* PeerCandidate::dgram_method = nullptr;

BIO* PeerCandidate::BIO_new_dgram(PeerCandidate* pc)
{
    std::call_once(dgram_once, [] {
        dgram_method = BIO_meth_new(BIO_TYPE_DGRAM, "dgram");
        BIO_meth_set_read(dgram_method, dgram_read);
        BIO_meth_set_write(dgram_method, dgram_write);
        BIO_meth_set_ctrl(dgram_method, dgram_ctrl);
        BIO_meth_set_destroy(dgram_method, dgram_free);
    });

    BIO* b = BIO_new(dgram_method);
    if (b == nullptr) {
        return nullptr;
    }

    BIO_set_init(b, 1);
    BIO_set_shutdown(b, 0);

    const auto ptr = new dgram_data{ pc };
    BIO_set_data(b, ptr);
    return b;
}

void PeerCandidate::freeDTLS()
{
    if (mDtlsSsl) {
        SSL_shutdown(mDtlsSsl);
        SSL_free(mDtlsSsl);
        mDtlsSsl = nullptr;
    }

    if (mDtlsCtx) {
        SSL_CTX_free(mDtlsCtx);
        mDtlsCtx = nullptr;
    }

    mDtlsReceiveQueue.clear();
    mDtlsBio = nullptr;
}

// State

void PeerCandidate::emitOnConnecting()
{
    mListener->onCandidateConnecting(this);
}

void PeerCandidate::emitOnIceSelected()
{
    mListener->onCandidateIceSelected(this);
}

void PeerCandidate::emitOnConnected()
{
    mListener->onCandidateConnected(this);

    mSrtpConnection->onPeerConnected();

    if (mExtensionSourceTWCC) {
        mExtensionSourceTWCC->onPeerConnected();
    }
}

void PeerCandidate::emitOnFailedToConnect(const Error& error)
{
    mListener->onCandidateFailedToConnect(this, error);
}

void PeerCandidate::onReceivedFromRemote()
{
    mLastReceiveTime = std::chrono::steady_clock::now();
    updateConnectionLostTimeout();

    Task::cancelHelper(mTaskConnectTimeout);
    Task::cancelHelper(mTaskConnectionRestoreTimeout);

    if (!mIsConnected) {
        mIsConnected = true;
        emitOnConnected();
    }
}

void PeerCandidate::sendStunBindingRequest(unsigned int iteration)
{
    LOG(SRTC_LOG_V, "Sending STUN binding request, iteration = %u, #%u", iteration, mUniqueId);

    const auto iceMessage = make_stun_message_binding_request(
        mIceAgent, mIceMessageBuffer.get(), kIceMessageBufferSize, mOffer, mAnswer, "start to connect", false);
    addSendRaw({ mIceMessageBuffer.get(), stun_message_length(&iceMessage) });

    mTaskSendStunConnectRequest = mScheduler.submit(kConnectRepeatPeriod + (iteration + 1) * kConnectRepeatIncrement,
                                                    __FILE__,
                                                    __LINE__,
                                                    [this, iteration] { sendStunBindingRequest(iteration + 1); });
}

void PeerCandidate::sendStunBindingResponse(unsigned int iteration)
{
    LOG(SRTC_LOG_V, "Sending STUN binding response #%u", mUniqueId);

    const auto iceMessage = make_stun_message_binding_request(
        mIceAgent, mIceMessageBuffer.get(), kIceMessageBufferSize, mOffer, mAnswer, "use candidate", true);

    addSendRaw({ mIceMessageBuffer.get(), stun_message_length(&iceMessage) });

    mTaskSendStunConnectResponse = mScheduler.submit(kConnectRepeatPeriod + (iteration + 1) * kConnectRepeatIncrement,
                                                     __FILE__,
                                                     __LINE__,
                                                     [this, iteration] { sendStunBindingResponse(iteration + 1); });
}

void PeerCandidate::updateConnectionLostTimeout()
{
    if (const auto task = mTaskConnectionLostTimeout.lock()) {
        mTaskConnectionLostTimeout = task->update(kConnectionLostTimeout);
    } else {
        mTaskConnectionLostTimeout =
            mScheduler.submit(kConnectionLostTimeout, __FILE__, __LINE__, [this] { onConnectionLostTimeout(); });
    }
}

void PeerCandidate::onConnectionLostTimeout()
{
    Task::cancelHelper(mTaskConnectionLostTimeout);

    mIsConnected = false;

    emitOnConnecting();

    LOG(SRTC_LOG_V, "Starting STUN requests to restore the connection #%u", mUniqueId);

    // Connecting should take a limited amount of time
    Task::cancelHelper(mTaskConnectTimeout);

    mTaskConnectTimeout = mScheduler.submit(kConnectTimeout, __FILE__, __LINE__, [this] {
        emitOnFailedToConnect({ Error::Code::InvalidData, "Connect timeout" });
    });

    // Send ice requests
    sendConnectionRestoreRequest();
}

void PeerCandidate::sendConnectionRestoreRequest()
{
    LOG(SRTC_LOG_V, "Sending a STUN request to restore the connection #%u", mUniqueId);

    const auto request = make_stun_message_binding_request(
        mIceAgent, mIceMessageBuffer.get(), kIceMessageBufferSize, mOffer, mAnswer, "connection restore", false);
    addSendRaw({ mIceMessageBuffer.get(), stun_message_length(&request) });

    Task::cancelHelper(mTaskConnectionRestoreTimeout);
    mTaskConnectionRestoreTimeout =
        mScheduler.submit(kConnectTimeout, __FILE__, __LINE__, [this] { sendConnectionRestoreRequest(); });
}

void PeerCandidate::updateKeepAliveTimeout()
{
    if (const auto task = mTaskKeepAliveTimeout.lock()) {
        mTaskKeepAliveTimeout = task->update(kKeepAliveCheckTimeout);
    } else {
        mTaskKeepAliveTimeout =
            mScheduler.submit(kKeepAliveCheckTimeout, __FILE__, __LINE__, [this] { onKeepAliveTimeout(); });
    }
}

void PeerCandidate::onKeepAliveTimeout()
{
    updateKeepAliveTimeout();

    const auto now = std::chrono::steady_clock::now();
    if (now - mLastSendTime < kKeepAliveSendTimeout && now - mLastReceiveTime < kKeepAliveSendTimeout) {
        return;
    }

    LOG(SRTC_LOG_V, "Sending a keep alive STUN request #%u", mUniqueId);

    const auto request = make_stun_message_binding_request(
        mIceAgent, mIceMessageBuffer.get(), kIceMessageBufferSize, mOffer, mAnswer, "keep-alive", false);
    addSendRaw({ mIceMessageBuffer.get(), stun_message_length(&request) });
}

} // namespace srtc

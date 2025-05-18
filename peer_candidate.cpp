#include "srtc/peer_candidate.h"
#include "srtc/event_loop.h"
#include "srtc/ice_agent.h"
#include "srtc/logging.h"
#include "srtc/packetizer.h"
#include "srtc/rtcp_packet.h"
#include "srtc/rtcp_packet_source.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_extension_source_simulcast.h"
#include "srtc/rtp_extension_source_twcc.h"
#include "srtc/rtp_std_extensions.h"
#include "srtc/sdp_answer.h"
#include "srtc/sdp_offer.h"
#include "srtc/send_history.h"
#include "srtc/srtc.h"
#include "srtc/srtp_connection.h"
#include "srtc/srtp_openssl.h"
#include "srtc/track.h"
#include "srtc/track_stats.h"
#include "srtc/util.h"
#include "srtc/x509_certificate.h"

#include <cassert>
#include <cstring>

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

constexpr auto kIceMessageBufferSize = 2048;

constexpr std::chrono::milliseconds kConnectTimeout = std::chrono::milliseconds(5000);
constexpr std::chrono::milliseconds kReceiveTimeout = std::chrono::milliseconds(5000);
constexpr std::chrono::milliseconds kExpireStunPeriod = std::chrono::milliseconds(1000);
constexpr std::chrono::milliseconds kExpireStunTimeout = std::chrono::milliseconds(5000);
constexpr std::chrono::milliseconds kKeepAliveCheckTimeout = std::chrono::milliseconds(1000);
constexpr std::chrono::milliseconds kKeepAliveSendTimeout = std::chrono::milliseconds(3000);
constexpr std::chrono::milliseconds kConnectRepeatTimeout = std::chrono::milliseconds(100);

// https://datatracker.ietf.org/doc/html/rfc5245#section-4.1.2.1
uint32_t make_stun_priority(int type_preference, int local_preference, uint8_t component_id)
{
	return (1 << 24) * type_preference + (1 << 8) * local_preference + (256 - component_id);
}

StunMessage make_stun_message_binding_request(const std::shared_ptr<srtc::IceAgent>& agent,
											  uint8_t* buf,
											  size_t len,
											  const std::shared_ptr<srtc::SdpOffer>& offer,
											  const std::shared_ptr<srtc::SdpAnswer>& answer,
											  bool useCandidate)
{
	StunMessage msg = {};
	agent->initRequest(&msg, buf, len, STUN_BINDING);

	if (useCandidate) {
		stun_message_append_flag(&msg, STUN_ATTRIBUTE_USE_CANDIDATE);
	}

	const uint32_t priority = make_stun_priority(200, 10, 1);
	stun_message_append32(&msg, STUN_ATTRIBUTE_PRIORITY, priority);

	// https://datatracker.ietf.org/doc/html/rfc5245#section-7.1.2.3
	const auto offerUserName = offer->getIceUFrag();
	const auto answerUserName = answer->getIceUFrag();
	const auto iceUserName = answerUserName + ":" + offerUserName;
	const auto icePassword = answer->getIcePassword();

	agent->finishMessage(&msg, iceUserName, icePassword);

	return msg;
}

StunMessage make_stun_message_binding_response(const std::shared_ptr<srtc::IceAgent>& agent,
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

	stun_message_append_xor_addr(&msg, STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS, &address.ss, addressLen);

	// https://datatracker.ietf.org/doc/html/rfc5245#section-7.1.2.3
	const auto icePassword = offer->getIcePassword();

	agent->finishMessage(&msg, srtc::nullopt, icePassword);

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

} // namespace

namespace srtc
{

PeerCandidate::PeerCandidate(PeerCandidateListener* const listener,
							 const std::shared_ptr<SdpOffer>& offer,
							 const std::shared_ptr<SdpAnswer>& answer,
							 const std::shared_ptr<RealScheduler>& scheduler,
							 const Host& host,
							 const std::shared_ptr<EventLoop>& eventLoop,
							 const Scheduler::Delay& startDelay)
	: mListener(listener)
	, mOffer(offer)
	, mAnswer(answer)
	, mHost(host)
	, mEventLoop(eventLoop)
	, mSocket(std::make_shared<Socket>(host.addr))
	, mIceAgent(std::make_shared<IceAgent>())
	, mIceMessageBuffer(std::make_unique<uint8_t[]>(kIceMessageBufferSize))
	, mSendHistory(std::make_shared<SendHistory>())
#ifdef NDEBUG
#else
	, mNoSendRandomGenerator(0, 99)
#endif
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
	, mExtensionSourceTWCC(RtpExtensionSourceTWCC::factory(offer, answer))
	, mLastSendTime(std::chrono::steady_clock::time_point::min())
	, mLastReceiveTime(std::chrono::steady_clock::time_point::min())
	, mScheduler(scheduler)
{
	assert(mListener);

	LOG(SRTC_LOG_V, "Constructor for %p %d", static_cast<void*>(this), mUniqueId);

	initOpenSSL();

	mEventLoop->registerSocket(mSocket->fd(), this);

	mScheduler.submit(startDelay, __FILE__, __LINE__, [this] { startConnecting(); });
}

PeerCandidate::~PeerCandidate()
{
	LOG(SRTC_LOG_V, "Destructor for %p %d", static_cast<void*>(this), mUniqueId);

	mEventLoop->unregisterSocket(mSocket->fd());

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

void PeerCandidate::sendRtcpPacket(const std::shared_ptr<RtcpPacket>& packet)
{
	if (mSrtp) {
		const auto track = packet->getTrack();
		const auto rtcpSource = track->getRtcpPacketSource();

		const auto packetData = packet->generate();

		ByteBuffer protectedData;
		if (mSrtp->protectOutgoingControl(packetData, rtcpSource->getNextSequence(), protectedData)) {
			const auto w = mSocket->send(protectedData.data(), protectedData.size());
			LOG(SRTC_LOG_V, "Sent %zu bytes of RTCP", w);
		}
	}
}

void PeerCandidate::updatePublishConnectionStats(PublishConnectionStats& stats) const
{
	if (mExtensionSourceTWCC) {

#ifndef NDEBUG
		if (mDebugTotalOutPackets > 0) {
			const auto percentLost =
				100.0f * static_cast<float>(mDebugDroppedOutPackets) / static_cast<float>(mDebugTotalOutPackets);
			std::printf(
				"*** Dropped %zu out of %zu packets, %.2f%%\n",
				mDebugDroppedOutPackets,
				mDebugTotalOutPackets,
				percentLost);
		}
#endif
		mExtensionSourceTWCC->updatePublishConnectionStats(stats);
	}
}

void PeerCandidate::process()
{
	const auto now = std::chrono::steady_clock::now();

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

		if (!item.csd.empty()) {
			// Codec Specific Data
			item.packetizer->setCodecSpecificData(item.csd);
		} else if (!item.buf.empty()) {
			// Frame data
			if (mExtensionSourceSimulcast) {
				if (item.track->isSimulcast() &&
					mExtensionSourceSimulcast->shouldAdd(item.track, item.packetizer, item.buf)) {
					std::vector<std::shared_ptr<SimulcastLayer>> layerList;
					for (const auto& trackItem : mAnswer->getVideoSimulcastTrackList()) {
						layerList.push_back(trackItem->getSimulcastLayer());
					}
					mExtensionSourceSimulcast->prepare(item.track, layerList);
				} else {
					mExtensionSourceSimulcast->clear();
				}
			}

			// Packetize
			const auto packetList = item.packetizer->generate(
				mExtensionSourceSimulcast, mExtensionSourceTWCC, mSrtp->getMediaProtectionOverhead(), item.buf);

			const auto stats = item.track->getStats();

			for (const auto& packet : packetList) {
				// Save
				if (item.track->hasNack() || item.track->getRtxPayloadId() > 0) {
					mSendHistory->save(packet);
				}

				// Generate
				const auto packetData = packet->generate();
				if (mSrtp) {
					ByteBuffer protectedData;
					if (mSrtp->protectOutgoingMedia(packetData.buf, packetData.rollover, protectedData)) {
						// Update send timeout
						mLastSendTime = now;

						// Keep stats
						stats->incrementSentPackets(1);
						stats->incrementSentBytes(protectedData.size());

						// Record in TWCC
						if (mExtensionSourceTWCC) {
							mExtensionSourceTWCC->onBeforeSendingRtpPacket(packet, protectedData.size());
						}

#ifdef NDEBUG
						// Send
						(void)mSocket->send(protectedData.data(), protectedData.size());
#else
						// In debug mode, we have deliberate 5% packet loss to make sure that NACK / RTX processing
						// works
						const auto& config = mOffer->getConfig();
						const auto randomValue = mNoSendRandomGenerator.next();
						if (config.debug_drop_packets && randomValue < 5 &&
							item.track->getMediaType() == MediaType::Video) {

							mDebugDroppedOutPackets += 1;

							uint16_t twcc;
							if (mExtensionSourceTWCC && mExtensionSourceTWCC->getFeedbackSeq(packet, twcc)) {
								LOG(SRTC_LOG_V, "NOT sending packet %u, twcc %u", packet->getSequence(), twcc);
							} else {
								LOG(SRTC_LOG_V, "NOT sending packet %u", packet->getSequence());
							}
						} else {
							const auto w = mSocket->send(protectedData.data(), protectedData.size());
							// This is too much
							// LOG(SRTC_LOG_V, "Sent %zu bytes of RTP media", w);
							(void)w;
						}

						mDebugTotalOutPackets += 1;
#endif
					}
				} else {
					LOG(SRTC_LOG_V, "SRTP is not initialized yet");
				}
			}
		}
	}

	// Receive
	while (!mRawReceiveQueue.empty()) {
		Socket::ReceivedData data = std::move(mRawReceiveQueue.front());
		mRawReceiveQueue.erase(mRawReceiveQueue.begin());

		if (is_stun_message(data.buf)) {
			LOG(SRTC_LOG_V, "Received STUN message %zd, %d", data.buf.size(), data.buf.data()[0]);
			onReceivedStunMessage(data);
		} else if (mDtlsSsl && is_dtls_message(data.buf)) {
			LOG(SRTC_LOG_V, "Received DTLS message %zd, %d", data.buf.size(), data.buf.data()[0]);
			onReceivedDtlsMessage(std::move(data.buf));
		} else if (is_rtc_message(data.buf)) {
			LOG(SRTC_LOG_V, "Received RTP/RTCP message size = %zd, id = %d", data.buf.size(), data.buf.data()[0]);
			onReceivedRtcMessage(std::move(data.buf));
		} else {
			LOG(SRTC_LOG_V, "Received unknown message %zd, %d", data.buf.size(), data.buf.data()[0]);
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
}

void PeerCandidate::startConnecting()
{
	// Start from scratch
	mDtlsState = DtlsState::Inactive;
	mSentUseCandidate = false;

	// Notify the listener
	emitOnConnecting();

	// We have a timeout
	mTaskConnectTimeout = mScheduler.submit(kConnectTimeout, __FILE__, __LINE__, [this] {
		emitOnFailedToConnect({ Error::Code::InvalidData, "Connect timeout" });
	});

	// Trim stun requests from time to time
	mTaskExpireStunRequests =
		mScheduler.submit(kExpireStunPeriod, __FILE__, __LINE__, [this] { forgetExpiredStunRequests(); });

	// Open the conversation by sending an STUN binding request
	sendStunBindingRequest();
}

void PeerCandidate::addSendRaw(ByteBuffer&& buf)
{
	mRawSendQueue.push_back(std::move(buf));
	mListener->onCandidateHasDataToSend(this);
}

void PeerCandidate::onReceivedStunMessage(const Socket::ReceivedData& data)
{
	StunMessage incomingMessage = {};
	incomingMessage.buffer = data.buf.data();
	incomingMessage.buffer_len = data.buf.size();

	const auto stunMessageClass = stun_message_get_class(&incomingMessage);
	const auto stunMessageMethod = stun_message_get_method(&incomingMessage);

	LOG(SRTC_LOG_V, "STUN message class  = %d", stunMessageClass);
	LOG(SRTC_LOG_V, "STUN message method = %d", stunMessageMethod);

	if (stunMessageClass == STUN_REQUEST && stunMessageMethod == STUN_BINDING) {
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
			addSendRaw({ mIceMessageBuffer.get(), stun_message_length(&response) });
		} else {
			LOG(SRTC_LOG_E, "STUN request verification failed, ignoring");
		}
	} else if (stunMessageClass == STUN_RESPONSE && stunMessageMethod == STUN_BINDING) {
		int errorCode = { 0 };
		if (stun_message_find_error(&incomingMessage, &errorCode) == STUN_MESSAGE_RETURN_SUCCESS) {
			LOG(SRTC_LOG_V, "STUN response error code: %d", errorCode);
		}

		uint8_t id[STUN_MESSAGE_TRANS_ID_LEN];
		stun_message_id(&incomingMessage, id);

		if (mIceAgent->forgetTransaction(id)) {
			LOG(SRTC_LOG_V, "Removed old STUN transaction ID for binding request");

			const auto icePassword = mAnswer->getIcePassword();

			if (errorCode == 0 && mIceAgent->verifyResponseMessage(&incomingMessage, icePassword)) {
				if (mSentUseCandidate) {
					// Keep-alive
					mLastReceiveTime = std::chrono::steady_clock::now();
					updateConnectionLostTimeout();
				} else {
					// Initial connection
					LOG(SRTC_LOG_V, "STUN binding response verification succeeded, sending use candidate request");

					mSentUseCandidate = true;

					Task::cancelHelper(mTaskSendStunConnectRequest);

					emitOnIceConnected();
					sendStunBindingResponse();

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
	mDtlsReceiveQueue.push_back(std::move(buf));

	// Try the handshake
	if (mDtlsState == DtlsState::Activating) {
		const auto r1 = SSL_do_handshake(mDtlsSsl);
		const auto err = SSL_get_error(mDtlsSsl, r1);
		LOG(SRTC_LOG_V, "DTLS handshake: %d, %d", r1, err);

		Task::cancelHelper(mTaskSendStunConnectResponse);

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
					const auto [srtpConn, srtpError] = SrtpConnection::create(mDtlsSsl, mAnswer->isSetupActive());

					if (srtpError.isOk()) {
						mSrtp = srtpConn;
						mDtlsState = DtlsState::Completed;

						const auto addr = to_string(mHost.addr);
						const auto cipher = SSL_get_cipher(mDtlsSsl);
						const auto profile = SSL_get_selected_srtp_profile(mDtlsSsl);
						LOG(SRTC_LOG_V,
							"Connected to %s with cipher %s, profile %s",
							addr.c_str(),
							cipher,
							profile->name);

						emitOnDtlsConnected();

						Task::cancelHelper(mTaskConnectTimeout);

						mLastReceiveTime = std::chrono::steady_clock::now();
						updateConnectionLostTimeout();

						updateKeepAliveTimeout();
					} else {
						// Error, failed to initialize SRTP
						LOG(SRTC_LOG_E,
							"Failed to initialize SRTP: %d, %s",
							static_cast<int>(srtpError.mCode),
							srtpError.mMessage.c_str());
						mDtlsState = DtlsState::Failed;
						emitOnFailedToConnect(srtpError);
					}
				} else {
					// Error, certificate hash does not match
					LOG(SRTC_LOG_E, "Server cert doesn't match the fingerprint");
					mDtlsState = DtlsState::Failed;
					emitOnFailedToConnect({ Error::Code::InvalidData, "Certificate hash doesn't match" });
				}
			}
		} else {
			// Error during DTLS handshake
			LOG(SRTC_LOG_E, "Failed during DTLS handshake");
			mDtlsState = DtlsState::Failed;
			emitOnFailedToConnect({ Error::Code::InvalidData, "Failure during DTLS handshake" });
		}

		if (mDtlsState == DtlsState::Failed) {
			freeDTLS();
		}
	}
}

void PeerCandidate::onReceivedRtcMessage(ByteBuffer&& buf)
{
	if (is_rtcp_message(buf) && mSrtp) {
		ByteBuffer output;
		if (mSrtp->unprotectIncomingControl(buf, output)) {
			LOG(SRTC_LOG_V, "RTCP unprotect: size = %zd", output.size());
			onReceivedRtcMessageUnprotected(output);
		}
	}
}

void PeerCandidate::onReceivedRtcMessageUnprotected(const ByteBuffer& buf)
{
	mLastReceiveTime = std::chrono::steady_clock::now();
	updateConnectionLostTimeout();

	const auto unprotectedSize = buf.size();
	ByteReader rtcpReader = { buf.data(), unprotectedSize };

	const auto rtcpRC = rtcpReader.readU8() & 0x1f;
	const auto rtcpPT = rtcpReader.readU8();
	const auto rtcpLength = 4 * (1 + rtcpReader.readU16());
	const auto rtcpSSRC = rtcpReader.readU32();

	LOG(SRTC_LOG_V, "RTCP payload = %d, len = %d, SSRC = %u", rtcpPT, rtcpLength, rtcpSSRC);

	if (rtcpPT == 205) {
		// https://datatracker.ietf.org/doc/html/rfc4585#section-6.2
		// RTPFB: Transport layer FB message
		if (rtcpReader.remaining() >= 4) {
			const auto rtcpFmt = rtcpRC;
			const auto rtcpSSRC_1 = rtcpReader.readU32();
			LOG(SRTC_LOG_V, "RTCP RTPFB FMT = %u, SSRC = %u", rtcpFmt, rtcpSSRC_1);

			switch (rtcpFmt) {
			case 1:
				// https://datatracker.ietf.org/doc/html/rfc4585#section-6.2.1
				onReceivedRtcMessage_205_1(rtcpSSRC_1, rtcpReader);
				break;
			case 15:
				// Google'e Transport-Wide Congension Control
				onReceivedRtcMessage_205_15(rtcpSSRC_1, rtcpReader);
				break;
			default:
				LOG(SRTC_LOG_V, "RTCP RTPFB Unknown fmt = %u", rtcpFmt);
				break;
			}
		}
	}
}

void PeerCandidate::onReceivedRtcMessage_205_1(uint32_t ssrc, ByteReader& rtcpReader)
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
			const auto packet = mSendHistory->find(ssrc, seq);

			// Record in TWCC
			if (packet && mExtensionSourceTWCC) {
				mExtensionSourceTWCC->onPacketWasNacked(packet);
			}

			if (packet && mSrtp) {
				// Generate
				const auto track = packet->getTrack();

				RtpPacket::Output packetData;
				if (track->getRtxPayloadId() > 0) {
					auto extension = packet->getExtension();

					if (track->isSimulcast() && mExtensionSourceSimulcast) {
						auto builder = RtpExtensionBuilder::from(extension);
						if (track->isSimulcast() && mExtensionSourceSimulcast) {
							mExtensionSourceSimulcast->updateForRtx(builder, track);
						}
						extension = builder.build();
					}

					packetData = packet->generateRtx(extension);
				} else {
					packetData = packet->generate();
				}

				ByteBuffer protectedData;
				if (mSrtp->protectOutgoingMedia(packetData.buf, packetData.rollover, protectedData)) {
					// And send
					const auto sentSize = mSocket->send(protectedData.data(), protectedData.size());
					LOG(SRTC_LOG_V,
						"Re-sent RTP packet with SSRC = %u, SEQ = %u, size = %zu, rtx = %d",
						packet->getSSRC(),
						packet->getSequence(),
						sentSize,
						packet->getTrack()->getRtxPayloadId() > 0);

					// Keep stats
					const auto stats = packet->getTrack()->getStats();
					stats->incrementSentPackets(1);
					stats->incrementSentBytes(protectedData.size());
				} else {
					LOG(SRTC_LOG_E, "Error protecting packet for re-sending");
				}
			} else {
				LOG(SRTC_LOG_V, "Cannot find packet with SSRC = %u, SEQ = %u for re-sending", ssrc, seq);
			}
		}
	}
}

void PeerCandidate::onReceivedRtcMessage_205_15(uint32_t ssrc, ByteReader& rtcpReader)
{
	if (rtcpReader.remaining() >= 8 && mExtensionSourceTWCC) {
		mExtensionSourceTWCC->onReceivedRtcpPacket(ssrc, rtcpReader);
	}
}

void PeerCandidate::forgetExpiredStunRequests()
{
	mIceAgent->forgetExpiredTransactions(kExpireStunTimeout);
	mScheduler.submit(kExpireStunPeriod, __FILE__, __LINE__, [this] { forgetExpiredStunRequests(); });
}

// Custom BIO for DGRAM

struct dgram_data {
	srtc::PeerCandidate* pc;
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
	auto ptr = BIO_get_data(b);
	auto data = reinterpret_cast<dgram_data*>(ptr);

	data->pc->addSendRaw({ reinterpret_cast<const uint8_t*>(in), static_cast<size_t>(inl) });

	return inl;
}

long PeerCandidate::dgram_ctrl(BIO* b, int cmd, long num, void* ptr)
{
	switch (cmd) {
	case BIO_CTRL_DUP:
	case BIO_CTRL_FLUSH:
		return 1;
#ifdef BIO_CTRL_GET_KTLS_SEND
	case BIO_CTRL_GET_KTLS_SEND:
		return 0;
#endif
#ifdef BIO_CTRL_GET_KTLS_RECV
	case BIO_CTRL_GET_KTLS_RECV:
		return 0;
#endif
	}
	return 0;
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

	mDtlsBio = nullptr;
}

// State

void PeerCandidate::emitOnConnecting()
{
	mListener->onCandidateConnecting(this);
}

void PeerCandidate::emitOnIceConnected()
{
	mListener->onCandidateIceConnected(this);
}

void PeerCandidate::emitOnDtlsConnected()
{
	mListener->onCandidateDtlsConnected(this);
}

void PeerCandidate::emitOnFailedToConnect(const Error& error)
{
	mListener->onCandidateFailedToConnect(this, error);
}

void PeerCandidate::emitOnLostConnection(const srtc::Error& error)
{
	mListener->onCandidateLostConnection(this, error);
}

void PeerCandidate::sendStunBindingRequest()
{
	LOG(SRTC_LOG_V, "Sending STUN binding request %d", mUniqueId);

	const auto iceMessage = make_stun_message_binding_request(
		mIceAgent, mIceMessageBuffer.get(), kIceMessageBufferSize, mOffer, mAnswer, false);
	addSendRaw({ mIceMessageBuffer.get(), stun_message_length(&iceMessage) });

	mTaskSendStunConnectRequest =
		mScheduler.submit(kConnectRepeatTimeout, __FILE__, __LINE__, [this] { sendStunBindingRequest(); });
}

void PeerCandidate::sendStunBindingResponse()
{
	LOG(SRTC_LOG_V, "Sending STUN binding response %d", mUniqueId);

	const auto iceMessage = make_stun_message_binding_request(
		mIceAgent, mIceMessageBuffer.get(), kIceMessageBufferSize, mOffer, mAnswer, true);

	addSendRaw({ mIceMessageBuffer.get(), stun_message_length(&iceMessage) });

	mTaskSendStunConnectResponse =
		mScheduler.submit(kConnectRepeatTimeout, __FILE__, __LINE__, [this] { sendStunBindingResponse(); });
}

void PeerCandidate::updateConnectionLostTimeout()
{
	if (const auto task = mTaskConnectionLostTimeout.lock()) {
		mTaskConnectionLostTimeout = task->update(kReceiveTimeout);
	} else {
		mTaskConnectionLostTimeout =
			mScheduler.submit(kReceiveTimeout, __FILE__, __LINE__, [this] { onConnectionLostTimeout(); });
	}
}

void PeerCandidate::onConnectionLostTimeout()
{
	if (const auto ptr = mTaskConnectionLostTimeout.lock()) {
		ptr->cancel();
	}

	emitOnLostConnection({ Error::Code::InvalidData, "Connection lost timeout" });
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
	if (now - mLastSendTime > kKeepAliveSendTimeout && now - mLastReceiveTime > kKeepAliveSendTimeout) {
		return;
	}

	LOG(SRTC_LOG_V, "Sending a keep alive STUN request %d", mUniqueId);

	const auto request = make_stun_message_binding_request(
		mIceAgent, mIceMessageBuffer.get(), kIceMessageBufferSize, mOffer, mAnswer, false);
	addSendRaw({ mIceMessageBuffer.get(), stun_message_length(&request) });
}

} // namespace srtc

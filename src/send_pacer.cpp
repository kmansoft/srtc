#include "srtc/send_pacer.h"
#include "srtc/logging.h"
#include "srtc/rtp_extension_source_twcc.h"
#include "srtc/rtp_packet.h"
#include "srtc/send_rtp_history.h"
#include "srtc/socket.h"
#include "srtc/srtp_connection.h"
#include "srtc/track.h"
#include "srtc/track_stats.h"

#include <algorithm>

#define LOG(level, ...) srtc::log(level, "SendPacer", __VA_ARGS__)

namespace srtc
{

SendPacer::SendPacer(const struct OfferConfig& offerConfig,
					 const std::shared_ptr<SrtpConnection>& srtp,
					 const std::shared_ptr<Socket>& socket,
					 const std::shared_ptr<SendRtpHistory>& history,
					 const std::shared_ptr<RtpExtensionSourceTWCC>& twcc,
					 const std::function<void()>& onSend)
	: mOfferConfig(offerConfig)
	, mSrtp(srtp)
	, mSocket(socket)
	, mHistory(history)
	, mTWCC(twcc)
	, mOnSend(onSend)
#ifdef NDEBUG
#else
	, mNoSendRandomGenerator(0, 99)
#endif
{
}

SendPacer::~SendPacer() = default;

void SendPacer::flush(const std::shared_ptr<Track>& track)
{
	size_t flushCount = 0;

	for (auto iter = mQueue.begin(); iter != mQueue.end();) {
		const auto packet = (*iter)->packet;
		if (packet->getTrack()->getSSRC() == track->getSSRC()) {
			iter = mQueue.erase(iter);
			sendImpl(packet);
			flushCount += 1;
		} else {
			++iter;
		}
	}

	(void) flushCount;

//	if (flushCount > 0) {
//		std::printf("*** Flushed %zu packets\n", flushCount);
//	}
}

void SendPacer::sendNow(const std::shared_ptr<RtpPacket>& packet)
{
	sendImpl(packet);
}

void SendPacer::sendPaced(const std::list<std::shared_ptr<RtpPacket>>& packetList, unsigned int spreadMillis)
{
	if (packetList.empty()) {
		return;
	}
	const auto size = packetList.size();
	if (size == 1) {
		sendImpl(packetList.front());
		return;
	}
	if (spreadMillis == 0) {
		for (const auto& packet : packetList) {
			sendImpl(packet);
		}
		return;
	}

	// Delta = desired spread / number of packets
	const auto delta = std::chrono::microseconds (1000 * spreadMillis / size);
	const auto now = std::chrono::steady_clock::now();

	unsigned int i = 0;
	for (const auto& packet : packetList) {
		const auto when = now + delta * i;
		const auto item = std::make_shared<Item>(when, packet);

		mQueue.insert(std::upper_bound(mQueue.begin(), mQueue.end(), item, ItemLess()), item);

		i += 1;
	}
}

[[nodiscard]] int SendPacer::getTimeoutMillis(int defaultValue) const
{
	if (!mQueue.empty()) {
		const auto when = mQueue.front()->when;
		const auto now = std::chrono::steady_clock::now();
		const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(when - now);
		return static_cast<int>(diff.count());
	}

	return defaultValue;
}

void SendPacer::run()
{
	for (auto iter = mQueue.begin(); iter != mQueue.end();) {
		if ((*iter)->when <= std::chrono::steady_clock::now()) {
			const auto packet = (*iter)->packet;
			iter = mQueue.erase(iter);
			sendImpl(packet);
		} else {
			break;
		}
	}
}

void SendPacer::sendImpl(const std::shared_ptr<RtpPacket>& packet)
{
	if (mTWCC) {
		mTWCC->onBeforeGeneratingRtpPacket(packet);
	}

	// Save
	const auto track = packet->getTrack();
	if (track->hasNack() || track->getRtxPayloadId() > 0) {
		mHistory->save(packet);
	}

	// Stats
	const auto stats = track->getStats();

	// Generate
	const auto packetData = packet->generate();
	ByteBuffer protectedData;
	if (mSrtp->protectOutgoingMedia(packetData.buf, packetData.rollover, protectedData)) {
		// Keep stats
		stats->incrementSentPackets(1);
		stats->incrementSentBytes(protectedData.size());

		// Record in TWCC
		if (mTWCC) {
			mTWCC->onBeforeSendingRtpPacket(packet, packetData.buf.size(), protectedData.size());
		}

		// Notify the sending callback
		if (mOnSend) {
			mOnSend();
		}

#ifdef NDEBUG
		// Send
		(void)mSocket->send(protectedData.data(), protectedData.size());
#else
		// In debug mode, we have deliberate 5% packet loss to validate that NACK / RTX processing works
		const auto randomValue = mNoSendRandomGenerator.next();
		if (mOfferConfig.debug_drop_packets && randomValue < 5 && track->getMediaType() == MediaType::Video) {
			uint16_t twcc;
			if (mTWCC && mTWCC->getFeedbackSeq(packet, twcc)) {
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
#endif
	}
}

} // namespace srtc

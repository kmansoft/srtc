#pragma once

#include <memory>
#include <vector>
#include <list>
#include <chrono>
#include <functional>

#include <cstdint>

#include "srtc/random_generator.h"
#include "srtc/sdp_offer.h"

namespace srtc
{

class SrtpConnection;
class Socket;
class SendRtpHistory;
class RtpPacket;
class RtpExtensionSourceTWCC;
class Track;

struct PubOfferConfig;

class SendPacer
{
public:
	SendPacer(const SdpOffer::Config& offerConfig,
			  const std::shared_ptr<SrtpConnection>& srtp,
			  const std::shared_ptr<Socket>& socket,
			  const std::shared_ptr<SendRtpHistory>& history,
			  const std::shared_ptr<RtpExtensionSourceTWCC>& twcc,
			  const std::function<void()>& onSend);
	~SendPacer();

	static constexpr auto kDefaultSpreadMillis = 15u;

	void flush(const std::shared_ptr<Track>& track);

	void sendNow(const std::shared_ptr<RtpPacket>& packet);
	void sendPaced(const std::list<std::shared_ptr<RtpPacket>>& packetList,
				   unsigned int spreadMillis);

	[[nodiscard]] int getTimeoutMillis(int defaultValue) const;
	void run();

private:
	const SdpOffer::Config mOfferConfig;
	const std::shared_ptr<SrtpConnection> mSrtp;
	const std::shared_ptr<Socket> mSocket;
	const std::shared_ptr<SendRtpHistory> mHistory;
	const std::shared_ptr<RtpExtensionSourceTWCC> mTWCC;
	const std::function<void()> mOnSend;

	struct Item {
		const std::chrono::steady_clock::time_point when;
		const std::shared_ptr<RtpPacket> packet;

		Item(const std::chrono::steady_clock::time_point& when,
			  const std::shared_ptr<RtpPacket>& packet) : when(when), packet(packet) {}
	};

	struct ItemLess {
		bool operator()(const std::shared_ptr<Item>& left, const std::shared_ptr<Item>& right)
		{
			return left->when < right->when;
		};
	};

	std::vector<std::shared_ptr<Item>> mQueue;

	void sendImpl(const std::shared_ptr<RtpPacket>& packet);

#ifdef NDEBUG
#else
	RandomGenerator<uint32_t> mLosePacketsRandomGenerator;
#endif

};

}
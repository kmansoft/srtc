#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <list>
#include <optional>
#include <string>

#include "stunmessage.h"

#include "srtc/random_generator.h"

namespace srtc
{

	class IceAgent
	{
	public:
		IceAgent();
		~IceAgent();

		// https://datatracker.ietf.org/doc/html/rfc5389#section-6
		static constexpr auto kRfc5389Cookie = 0x2112A442;

		bool initRequest(stun::StunMessage *msg, uint8_t *buffer, size_t buffer_len, stun::StunMethod m);
		bool initResponse(stun::StunMessage *msg, uint8_t *buffer, size_t buffer_len, const stun::StunMessage *request);
		bool finishMessage(stun::StunMessage *msg, const std::optional<std::string> &username, const std::string &password);

		bool forgetTransaction(stun::StunTransactionId id);

		void forgetExpiredTransactions(const std::chrono::milliseconds &expiration);

		bool verifyRequestMessage(stun::StunMessage *msg, const std::string &username, const std::string &password);
		bool verifyResponseMessage(stun::StunMessage *msg, const std::string &password);

	private:
		struct SavedTransaction
		{
			SavedTransaction(std::chrono::steady_clock::time_point when, const stun::StunTransactionId &id)
				: when(when), id()
			{
				std::memcpy(this->id, id, sizeof(stun::StunTransactionId));
			}

			std::chrono::steady_clock::time_point when;
			stun::StunTransactionId id;
		};

		RandomGenerator<uint32_t> mRandom;

		const uint64_t mTie;
		std::list<SavedTransaction> mTransactionList;
	};

} // namespace srtc

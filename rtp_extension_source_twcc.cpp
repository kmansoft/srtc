#include "srtc/rtp_extension_source_twcc.h"
#include "srtc/extension_map.h"
#include "srtc/logging.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_packet.h"
#include "srtc/rtp_std_extensions.h"
#include "srtc/sdp_answer.h"
#include "srtc/sdp_offer.h"
#include "srtc/track.h"
#include "srtc/twcc.h"

#include <cstring>
#include <functional>
#include <map>
#include <memory>

#define LOG(level, ...) srtc::log(level, "TWCC", __VA_ARGS__)

namespace
{

uint8_t findTWCCExtension(const srtc::ExtensionMap& map)
{
	return map.findByName(srtc::RtpStandardExtensions::kExtGoogleTWCC);
}

} // namespace

namespace srtc
{

RtpExtensionSourceTWCC::RtpExtensionSourceTWCC(uint8_t nVideoExtTWCC, uint8_t nAudioExtTWCC)
	: mVideoExtTWCC(nVideoExtTWCC)
	, mAudioExtTWCC(nAudioExtTWCC)
	, mNextPacketSEQ(1)
	, mPacketHistory(std::make_shared<twcc::PacketStatusHistory>())
	, mHeaderHistory(std::make_shared<twcc::FeedbackHeaderHistory>())
{
}

RtpExtensionSourceTWCC::~RtpExtensionSourceTWCC() = default;

std::shared_ptr<RtpExtensionSourceTWCC> RtpExtensionSourceTWCC::factory(const std::shared_ptr<SdpOffer>& offer,
																		const std::shared_ptr<SdpAnswer>& answer)
{
	const auto& config = offer->getConfig();
	if (!config.enable_bwe) {
		return {};
	}

	uint8_t nVideoExtTWCC = 0;
	if (answer->hasVideoMedia()) {
		nVideoExtTWCC = findTWCCExtension(answer->getVideoExtensionMap());
		if (nVideoExtTWCC == 0) {
			return {};
		}
	}

	uint8_t nAudioExtTWCC = 0;
	if (answer->hasAudioMedia()) {
		nAudioExtTWCC = findTWCCExtension(answer->getAudioExtensionMap());
		if (nAudioExtTWCC == 0) {
			return {};
		}
	}

	return std::make_shared<RtpExtensionSourceTWCC>(nVideoExtTWCC, nAudioExtTWCC);
}

bool RtpExtensionSourceTWCC::wants(const std::shared_ptr<Track>& track,
								   [[maybe_unused]] bool isKeyFrame,
								   [[maybe_unused]] int packetNumber)
{
	const auto media = track->getMediaType();
	return media == MediaType::Video && mVideoExtTWCC != 0 || media == MediaType::Audio && mAudioExtTWCC != 0;
}

void RtpExtensionSourceTWCC::add(RtpExtensionBuilder& builder,
								 const std::shared_ptr<Track>& track,
								 [[maybe_unused]] bool isKeyFrame,
								 [[maybe_unused]] int packetNumber)
{
	const auto seq = mNextPacketSEQ;
	mNextPacketSEQ += 1;

	const auto media = track->getMediaType();
	if (const auto id = mVideoExtTWCC; media == MediaType::Video && id != 0) {
		// Video media
		builder.addU16Value(id, seq);
	} else if (const auto id = mAudioExtTWCC; media == MediaType::Audio && id != 0) {
		// Audio media
		builder.addU16Value(id, seq);
	}
}

void RtpExtensionSourceTWCC::onBeforeSendingRtpPacket(const std::shared_ptr<RtpPacket>& packet)
{
	uint16_t seq;
	if (!getFeedbackSeq(packet, seq)) {
		return;
	}

	mPacketHistory->save(seq);
}

void RtpExtensionSourceTWCC::onPacketWasNacked(const std::shared_ptr<RtpPacket>& packet)
{
	uint16_t seq;
	if (!getFeedbackSeq(packet, seq)) {
		return;
	}

	const auto ptr = mPacketHistory->get(seq);
	if (ptr) {
		ptr->nack_count += 1;
	}
}

void RtpExtensionSourceTWCC::onReceivedRtcpPacket(uint32_t ssrc, ByteReader& reader)
{
	if (reader.remaining() < 8) {
		LOG(SRTC_LOG_E, "RTCP TWCC packet too small");
		return;
	}

	const uint16_t base_seq_number = reader.readU16();
	const uint16_t packet_status_count = reader.readU16();
	const uint32_t reference_time_and_fb_pkt_count = reader.readU32();

	const auto header =
		std::make_shared<twcc::FeedbackHeader>(base_seq_number,
											   packet_status_count,
											   static_cast<int32_t>(reference_time_and_fb_pkt_count) >> 8,
											   static_cast<uint8_t>(reference_time_and_fb_pkt_count) & 0xFF);

	const auto packetSymbolList = std::make_unique<uint8_t[]>(header->packet_status_count);
	std::memset(packetSymbolList.get(), 0, sizeof(uint8_t) * header->packet_status_count);

	// Be careful, this can wrap (and that's OK)
	const uint16_t past_end_seq_number = header->base_seq_number + header->packet_status_count;

	// Read the chunks
	for (uint16_t seq_number = header->base_seq_number; seq_number != past_end_seq_number;
		 /* do not increment here */) {
		if (reader.remaining() < 2) {
			LOG(SRTC_LOG_E, "RTCP TWCC packet too small while reading chunk header");
			return;
		}

		const auto chunkHeader = reader.readU16();
		const auto chunkType = (chunkHeader >> 15) & 0x01;

		if (chunkType == twcc::kCHUNK_RUN_LENGTH) {
			// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.3
			const auto symbol = (chunkHeader >> 13) & 0x03;
			const auto runLength = chunkHeader & 0x1FFF;
			const uint16_t remaining = past_end_seq_number - seq_number;
			if (remaining < runLength || remaining > 0xFFFF) {
				LOG(SRTC_LOG_E, "RTCP TWCC packet: run_length %u is too large, remaining %u", runLength, remaining);
				break;
			}

			if (runLength > 1000) {
				LOG(SRTC_LOG_E,
					"RTCP TWCC packet: run_length %u, symbol %d, packet_status_count %u, packet size %lu",
					runLength,
					symbol,
					header->packet_status_count,
					reader.size());
			}

			for (uint16_t j = 0; j < runLength; ++j) {
				const auto index = (seq_number + 0x10000 - header->base_seq_number) & 0xffff;
				packetSymbolList[index] = symbol;

				seq_number += 1;
				if (seq_number == past_end_seq_number) {
					break;
				}
			}
		} else if (chunkType == twcc::kCHUNK_STATUS_VECTOR && ((chunkHeader >> 14) & 0x01) == 0) {
			// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
			for (uint16_t shift = 14; shift != 0; shift -= 1) {
				const auto symbol =
					((chunkHeader >> (shift - 1)) & 0x01) ? twcc::kSTATUS_RECEIVED_NO_TS : twcc::kSTATUS_NOT_RECEIVED;

				const auto index = (seq_number + 0x10000 - header->base_seq_number) & 0xffff;
				packetSymbolList[index] = symbol;

				seq_number += 1;
				if (seq_number == past_end_seq_number) {
					break;
				}
			}
		} else if (chunkType == twcc::kCHUNK_STATUS_VECTOR && ((chunkHeader >> 14) & 0x01) == 1) {
			// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
			for (uint16_t shift = 14; shift != 0; shift -= 2) {
				const auto symbol = (chunkHeader >> (shift - 2)) & 0x03;

				const auto index = (seq_number + 0x10000 - header->base_seq_number) & 0xffff;
				packetSymbolList[index] = symbol;

				seq_number += 1;
				if (seq_number == past_end_seq_number) {
					break;
				}
			}
		} else {
			LOG(SRTC_LOG_E, "RTCP TWCC packet: unknown chunk type %u", chunkType);
			return;
		}
	}

	// Read the time deltas
	for (uint16_t i = 0; i < header->packet_status_count; ++i) {
		const auto symbol = packetSymbolList[i];
		const auto ptr = mPacketHistory->get(header->base_seq_number + i);

		if (symbol == twcc::kSTATUS_RECEIVED_SMALL_DELTA) {
			if (reader.remaining() < 1) {
				LOG(SRTC_LOG_E, "RTCP TWCC packet too small while reading small delta");
				return;
			}

			const auto delta = reader.readU8();
			if (ptr) {
				ptr->reported_status = twcc::kSTATUS_RECEIVED_SMALL_DELTA;
				ptr->reported_delta_micros = 250 * delta;
			}
		} else if (ptr->reported_status == twcc::kSTATUS_RECEIVED_LARGE_DELTA) {
			if (reader.remaining() < 2) {
				LOG(SRTC_LOG_E, "RTCP TWCC packet too small while reading large delta");
				return;
			}

			const auto delta = static_cast<int16_t>(reader.readU16());
			if (ptr) {
				ptr->reported_status = twcc::kSTATUS_RECEIVED_LARGE_DELTA;
				ptr->reported_delta_micros = 250 * delta;
			}
		}
	}

	mHeaderHistory->save(header);
}

bool RtpExtensionSourceTWCC::getFeedbackSeq(const std::shared_ptr<RtpPacket>& packet, uint16_t& outSeq) const
{
	const auto track = packet->getTrack();

	uint8_t nExtId = 0;
	if (track->getMediaType() == MediaType::Video) {
		nExtId = mVideoExtTWCC;
	} else if (track->getMediaType() == MediaType::Audio) {
		nExtId = mAudioExtTWCC;
	}
	if (nExtId == 0) {
		return false;
	}

	const auto& ext = packet->getExtension();
	if (ext.empty()) {
		return false;
	}

	ByteReader reader(ext.getData());
	while (reader.remaining() >= 2) {
		const auto id = reader.readU8();
		const auto len = reader.readU8();
		if (id == nExtId) {
			outSeq = reader.readU16();
			return true;
		}
		if (reader.remaining() < len) {
			break;
		}
		reader.skip(len);
	}
	return false;
}

float RtpExtensionSourceTWCC::getPacketsLostPercent() const
{
	return mPacketHistory->getPacketsLostPercent();
}
} // namespace srtc
#include "srtc/rtp_extension_source_twcc.h"
#include "srtc/extension_map.h"
#include "srtc/logging.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_packet.h"
#include "srtc/rtp_std_extensions.h"
#include "srtc/sdp_answer.h"
#include "srtc/sdp_offer.h"
#include "srtc/track.h"
#include "srtc/twcc_publish.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <memory>

#define LOG(level, ...) srtc::log(level, "TWCC", __VA_ARGS__)

namespace
{

uint8_t findTWCCExtension(const srtc::ExtensionMap& map)
{
    return map.findByName(srtc::RtpStandardExtensions::kExtGoogleTWCC);
}

bool isReceivedWithTime(uint8_t status)
{
    return status == srtc::twcc::kSTATUS_RECEIVED_SMALL_DELTA || status == srtc::twcc::kSTATUS_RECEIVED_LARGE_DELTA;
}

constexpr std::chrono::milliseconds kStartProbingTimeout = std::chrono::milliseconds(10 * 1000);
constexpr std::chrono::milliseconds kPeriodicProbingTimeout = std::chrono::milliseconds(5 * 1000);
constexpr std::chrono::milliseconds kProbeDuration = std::chrono::milliseconds(1000);

} // namespace

namespace srtc
{

RtpExtensionSourceTWCC::RtpExtensionSourceTWCC(uint8_t nVideoExtTWCC,
                                               uint8_t nAudioExtTWCC,
                                               const std::shared_ptr<RealScheduler>& scheduler)
    : mVideoExtTWCC(nVideoExtTWCC)
    , mAudioExtTWCC(nAudioExtTWCC)
    , mNextPacketSEQ(1)
    , mPacketHistory(std::make_unique<twcc::PublishPacketHistory>())
    , mIsConnected(false)
    , mIsProbing(false)
    , mProbingPacketCount(0)
    , mScheduler(scheduler)
{
}

RtpExtensionSourceTWCC::~RtpExtensionSourceTWCC() = default;

std::shared_ptr<RtpExtensionSourceTWCC> RtpExtensionSourceTWCC::factory(const std::shared_ptr<SdpOffer>& offer,
                                                                        const std::shared_ptr<SdpAnswer>& answer,
                                                                        const std::shared_ptr<RealScheduler>& scheduler)
{
    if (offer->getDirection() != Direction::Publish) {
        return {};
    }

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

    return std::make_shared<RtpExtensionSourceTWCC>(nVideoExtTWCC, nAudioExtTWCC, scheduler);
}

void RtpExtensionSourceTWCC::onPeerConnected()
{
    if (!mIsConnected) {
        mIsConnected = true;
        mTaskStartProbing = mScheduler.submit(kStartProbingTimeout, __FILE__, __LINE__, [this] { onStartProbing(); });
    }
}

uint8_t RtpExtensionSourceTWCC::getPadding(const std::shared_ptr<Track>& track, size_t remainingDataSize)
{
    if (mIsProbing) {
        if (remainingDataSize < 500) {
            return 50;
        }

        const auto mediaType = track->getMediaType();
        if (mediaType == MediaType::Video) {
            // Video gets packetized, we can always add 10% to outgoing packets
            mProbingPacketCount += 1;
            return 120;
        } else if (mediaType == MediaType::Audio) {
            // Audio doesn't create split packets, we have to stay within the MTU
            if (remainingDataSize < 1060) {
                mProbingPacketCount += 1;
                return remainingDataSize / 10;
            }
        }
    }
    return 0;
}

bool RtpExtensionSourceTWCC::wantsExtension(const std::shared_ptr<Track>& track,
                                            [[maybe_unused]] bool isKeyFrame,
                                            [[maybe_unused]] int packetNumber) const
{
    return getExtensionId(track) != 0;
}

void RtpExtensionSourceTWCC::addExtension(RtpExtensionBuilder& builder,
                                          const std::shared_ptr<Track>& track,
                                          [[maybe_unused]] bool isKeyFrame,
                                          [[maybe_unused]] int packetNumber)
{
    // Because of pacing, we don't assign a sequence number here, we do it before generating. But we still want to
    // write a placeholder so that packet size measurement works correctly.
    if (const auto id = getExtensionId(track); id != 0) {
        builder.addU16Value(id, 0);
    }
}

void RtpExtensionSourceTWCC::onBeforeGeneratingRtpPacket(const std::shared_ptr<RtpPacket>& packet)
{
    const auto track = packet->getTrack();
    if (const auto id = getExtensionId(track); id != 0) {
        auto builder = RtpExtensionBuilder::from(packet->getExtension());

        const auto seq = mNextPacketSEQ;
        mNextPacketSEQ += 1;

        builder.addOrReplaceU16Value(id, seq);

        packet->setExtension(builder.build());
    }
}

void RtpExtensionSourceTWCC::onBeforeSendingRtpPacket(const std::shared_ptr<RtpPacket>& packet,
                                                      size_t generatedSize,
                                                      size_t encryptedSize)
{
    const auto seq = getFeedbackSeq(packet);
    if (!seq.has_value()) {
        return;
    }

    const auto track = packet->getTrack();
    const auto paddingSize = packet->getPaddingSize();
    const auto payloadSize = packet->getPayloadSize();

    mPacketHistory->saveOutgoingPacket(seq.value(), track, paddingSize, payloadSize, generatedSize, encryptedSize);
}

void RtpExtensionSourceTWCC::onPacketWasNacked(const std::shared_ptr<RtpPacket>& packet)
{
    const auto seq = getFeedbackSeq(packet);
    if (!seq.has_value()) {
        return;
    }

    const auto ptr = mPacketHistory->get(seq.value());
    if (ptr) {
        ptr->nack_count += 1;
    }
}

//	https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1

void RtpExtensionSourceTWCC::onReceivedRtcpPacket(uint32_t ssrc, ByteReader& reader)
{
    if (reader.remaining() < 8) {
        LOG(SRTC_LOG_E, "RTCP TWCC packet too small");
        return;
    }

    const uint16_t base_seq_number = reader.readU16();
    const uint16_t packet_status_count = reader.readU16();
    const uint32_t reference_time_and_fb_pkt_count = reader.readU32();

    if (packet_status_count == 0) {
        LOG(SRTC_LOG_E, "RTCP TWCC packet has no data");
        return;
    }

    const auto reference_time = static_cast<int32_t>(reference_time_and_fb_pkt_count >> 8);
    const auto fb_pkt_count = static_cast<uint8_t>(reference_time_and_fb_pkt_count & 0xFFu);
    (void) fb_pkt_count;

    const auto reference_time_micros = 64 * 1000 * static_cast<int64_t>(reference_time);

    const auto tempList = mTempPacketBuffer.ensure(packet_status_count);
    std::memset(tempList, 0, sizeof(TempPacket) * packet_status_count);

    // Be careful, this can wrap (and that's OK)
    const auto past_end_seq_number = static_cast<uint16_t>(base_seq_number + packet_status_count);

    // Read the chunks
    for (uint16_t seq_number = base_seq_number; seq_number != past_end_seq_number; /* do not increment */) {
        if (reader.remaining() < 2) {
            LOG(SRTC_LOG_E, "RTCP TWCC packet too small while reading chunk header");
            return;
        }

        const auto chunkHeader = reader.readU16();
        const auto chunkType = (chunkHeader >> 15) & 0x01;

        if (chunkType == twcc::kCHUNK_RUN_LENGTH) {
            // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.3
            const auto symbol = (chunkHeader >> 13) & 0x03u;
            const auto runLength = chunkHeader & 0x1FFFu;
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
                    packet_status_count,
                    reader.size());
            }

            for (unsigned int j = 0; j < runLength; ++j) {
                const auto index = (seq_number + 0x10000 - base_seq_number) & 0xffff;
                assert(index >= 0);
                assert(index < packet_status_count);
                tempList[index].status = symbol;

                seq_number += 1;
                if (seq_number == past_end_seq_number) {
                    break;
                }
            }
        } else if (chunkType == twcc::kCHUNK_STATUS_VECTOR && ((chunkHeader >> 14) & 0x01) == 0) {
            // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
            for (uint16_t shift = 14; shift != 0; shift -= 1) {
                const auto symbol = ((chunkHeader >> (shift - 1)) & 0x01) ? twcc::kSTATUS_RECEIVED_SMALL_DELTA
                                                                          : twcc::kSTATUS_NOT_RECEIVED;

                const auto index = (seq_number + 0x10000 - base_seq_number) & 0xffff;
                assert(index >= 0);
                assert(index < packet_status_count);
                tempList[index].status = symbol;

                seq_number += 1;
                if (seq_number == past_end_seq_number) {
                    break;
                }
            }
        } else if (chunkType == twcc::kCHUNK_STATUS_VECTOR && ((chunkHeader >> 14) & 0x01) == 1) {
            // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
            for (uint16_t shift = 14; shift != 0; shift -= 2) {
                const auto symbol = (chunkHeader >> (shift - 2)) & 0x03;

                const auto index = (seq_number + 0x10000 - base_seq_number) & 0xffff;
                assert(index >= 0);
                assert(index < packet_status_count);
                tempList[index].status = symbol;

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
    for (uint16_t i = 0; i < packet_status_count; ++i) {
        const auto symbol = tempList[i].status;
        const auto ptr = mPacketHistory->get(base_seq_number + i);

        if (symbol == twcc::kSTATUS_RECEIVED_SMALL_DELTA) {
            if (reader.remaining() < 1) {
                LOG(SRTC_LOG_E, "RTCP TWCC packet too small while reading small delta");
                return;
            }

            const auto delta = reader.readU8();
            tempList[i].delta_micros = 250 * delta;
            if (ptr) {
                ptr->reported_status = twcc::kSTATUS_RECEIVED_SMALL_DELTA;
            }
        } else if (symbol == twcc::kSTATUS_RECEIVED_LARGE_DELTA) {
            if (reader.remaining() < 2) {
                LOG(SRTC_LOG_E, "RTCP TWCC packet too small while reading large delta");
                return;
            }

            const auto delta = static_cast<int16_t>(reader.readU16());
            tempList[i].delta_micros = 250 * delta;
            if (ptr) {
                ptr->reported_status = twcc::kSTATUS_RECEIVED_LARGE_DELTA;
            }
        }
    }

#if 0
	auto count_not_received = 0u;
	auto count_small_delta = 0u;
	auto count_large_delta = 0u;

	for (uint16_t i = 0; i < packet_status_count; ++i) {
		const auto symbol = tempList[i].status;
		switch (symbol) {
		case twcc::kSTATUS_NOT_RECEIVED:
			count_not_received += 1;
			break;
		case twcc::kSTATUS_RECEIVED_SMALL_DELTA:
			count_small_delta += 1;
			break;
		case twcc::kSTATUS_RECEIVED_LARGE_DELTA:
			count_large_delta += 1;
			break;
		default:
			break;
		}
	}

	std::printf("TWCC packets base = %u, count = %u, not received = %u, small delta = %u, large delta = %u\n",
				base_seq_number,
				packet_status_count,
				count_not_received,
				count_small_delta,
				count_large_delta);
#endif

    twcc::PublishPacket* prev_ptr = nullptr;

    if (isReceivedWithTime(tempList[0].status)) {
        const auto curr_seq = base_seq_number;
        const auto curr_ptr = mPacketHistory->get(curr_seq);
        if (curr_ptr) {
            prev_ptr = curr_ptr;
            prev_ptr->received_time_micros = reference_time_micros + tempList[0].delta_micros;
            prev_ptr->received_time_present = true;
        }
    }
    for (size_t i = 1; i < packet_status_count && prev_ptr; i += 1) {
        if (isReceivedWithTime(tempList[i].status)) {
            const uint16_t curr_seq = base_seq_number + i;
            const auto curr_ptr = mPacketHistory->get(curr_seq);
            if (curr_ptr) {
                curr_ptr->received_time_micros = prev_ptr->received_time_micros + tempList[i].delta_micros;
                curr_ptr->received_time_present = true;
            }

            prev_ptr = curr_ptr;
        } else {
            break;
        }
    }

    mPacketHistory->update();

    // If we are probing, and it starts causing increased delays or high packet loss, stop
    if (mIsProbing && mPacketHistory->shouldStopProbing()) {
        LOG(SRTC_LOG_V, "Stopping probing because of increasing inter delays or packet loss");
        Task::cancelHelper(mTaskEndProbing);
        mIsProbing = false;
    }
}

std::optional<uint16_t> RtpExtensionSourceTWCC::getFeedbackSeq(const std::shared_ptr<RtpPacket>& packet) const
{
    const auto track = packet->getTrack();
    const auto nExtId = getExtensionId(track);
    if (nExtId == 0) {
        return {};
    }

    const auto& ext = packet->getExtension();
    return ext.findU16(nExtId);
}

unsigned int RtpExtensionSourceTWCC::getPacingSpreadMillis(const std::list<std::shared_ptr<RtpPacket>>& list,
                                                           float bandwidthScale,
                                                           unsigned int defaultValue) const
{
    size_t totalSize = 0;
    for (const auto& packet : list) {
        totalSize += packet->getPayloadSize();
    }

    return mPacketHistory->getPacingSpreadMillis(totalSize, bandwidthScale, defaultValue);
}

void RtpExtensionSourceTWCC::updatePublishConnectionStats(PublishConnectionStats& stats) const
{
    mPacketHistory->updatePublishConnectionStats(stats);
}

uint8_t RtpExtensionSourceTWCC::getExtensionId(const std::shared_ptr<Track>& track) const
{
    const auto media = track->getMediaType();
    if (media == MediaType::Video) {
        return mVideoExtTWCC;
    } else if (media == MediaType::Audio) {
        return mAudioExtTWCC;
    }
    return 0;
}

void RtpExtensionSourceTWCC::onStartProbing()
{
    LOG(SRTC_LOG_V, "Start probing");

    mIsProbing = true;
    mProbingPacketCount = 0;

    // End this probing period
    mTaskEndProbing = mScheduler.submit(kProbeDuration, __FILE__, __LINE__, [this] { onEndProbing(); });

    // Start the next one
    mTaskStartProbing = mScheduler.submit(kPeriodicProbingTimeout, __FILE__, __LINE__, [this] { onStartProbing(); });
}

void RtpExtensionSourceTWCC::onEndProbing()
{
    LOG(SRTC_LOG_V, "End probing, %u packets", mProbingPacketCount);

    mIsProbing = false;
}

} // namespace srtc
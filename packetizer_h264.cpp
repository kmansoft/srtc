#include "srtc/packetizer_h264.h"
#include "srtc/h264.h"
#include "srtc/logging.h"
#include "srtc/rtp_extension.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_extension_source.h"
#include "srtc/rtp_packet.h"
#include "srtc/rtp_packet_source.h"
#include "srtc/rtp_time_source.h"
#include "srtc/track.h"

#include <list>

#define LOG(level, ...) srtc::log(level, "H264_pktzr", __VA_ARGS__)

namespace
{

// https://datatracker.ietf.org/doc/html/rfc6184#section-5.4

constexpr uint8_t STAP_A = 24;
constexpr uint8_t FU_A = 28;
constexpr size_t kMinPayloadSize = 600;

uint8_t getPadding(const std::shared_ptr<srtc::Track>& track,
				   const std::shared_ptr<srtc::RtpExtensionSource>& simulcast,
				   const std::shared_ptr<srtc::RtpExtensionSource>& twcc,
				   size_t remainingDataSize)
{
	if (remainingDataSize < 300) {
		return 0;
	}

	uint8_t padding = 0;

	if (simulcast) {
		const auto p = simulcast->getPadding(track, remainingDataSize);
		padding = std::max(padding, p);
	}

	if (twcc) {
		const auto p = twcc->getPadding(track, remainingDataSize);
		padding = std::max(padding, p);
	}

	return padding;
}

srtc::RtpExtension buildExtension(const std::shared_ptr<srtc::Track>& track,
								  const std::shared_ptr<srtc::RtpExtensionSource>& simulcast,
								  const std::shared_ptr<srtc::RtpExtensionSource>& twcc,
								  bool isKeyFrame,
								  int packetNumber)
{
	srtc::RtpExtension extension;

	const auto wantsSimulcast = simulcast && simulcast->wantsExtension(track, isKeyFrame, packetNumber);
	const auto wantsTWCC = twcc && twcc->wantsExtension(track, isKeyFrame, packetNumber);

	if (wantsSimulcast || wantsTWCC) {
		srtc::RtpExtensionBuilder builder;

		if (wantsSimulcast) {
			simulcast->addExtension(builder, track, isKeyFrame, packetNumber);
		}
		if (wantsTWCC) {
			twcc->addExtension(builder, track, isKeyFrame, packetNumber);
		}

		extension = builder.build();
	}

	return extension;
}

size_t adjustPacketSize(size_t basicPacketSize, size_t padding, const srtc::RtpExtension& extension)
{
	auto sizeLessPadding = basicPacketSize;
	if (padding > 0 && padding <= basicPacketSize / 2) {
		sizeLessPadding -= padding;
	}

	const auto extensionSize = extension.size();
	if (extensionSize == 0) {
		return sizeLessPadding;
	}

	// We need to be careful with unsigned math
	if (extensionSize + kMinPayloadSize > basicPacketSize) {
		return sizeLessPadding;
	}

	return sizeLessPadding - extensionSize;
}

} // namespace

namespace srtc
{

using namespace h264;

PacketizerH264::PacketizerH264(const std::shared_ptr<Track>& track)
	: Packetizer(track)
{
}

PacketizerH264::~PacketizerH264() = default;

void PacketizerH264::setCodecSpecificData(const std::vector<ByteBuffer>& csd)
{
	mSPS.clear();
	mPPS.clear();

	int target = 0;
	for (const auto& item : csd) {
		for (NaluParser parser(item); parser; parser.next()) {
			if (target == 0) {
				mSPS.assign(parser.currData(), parser.currDataSize());
				target += 1;
			} else if (target == 1) {
				mPPS.assign(parser.currData(), parser.currDataSize());
				target += 1;
			}
		}
	}
}

bool PacketizerH264::isKeyFrame(const ByteBuffer& frame) const
{
	for (NaluParser parser(frame); parser; parser.next()) {
		const auto naluType = parser.currType();
		if (naluType == NaluType::KeyFrame) {
			return true;
		}
	}

	return false;
}

std::list<std::shared_ptr<RtpPacket>> PacketizerH264::generate(const std::shared_ptr<RtpExtensionSource>& simulcast,
															   const std::shared_ptr<RtpExtensionSource>& twcc,
															   size_t mediaProtectionOverhead,
															   const srtc::ByteBuffer& frame)
{
	std::list<std::shared_ptr<RtpPacket>> result;

	// https://datatracker.ietf.org/doc/html/rfc6184

	bool addedParameters = false;

	const auto track = getTrack();

	const auto timeSource = track->getRtpTimeSource();
	const auto packetSource = track->getRtpPacketSource();

	const auto frameTimestamp = timeSource->getCurrTimestamp();

	for (NaluParser parser(frame); parser; parser.next()) {
		const auto naluType = parser.currType();

		if (naluType == NaluType::SPS) {
			// Update SPS
			mSPS.assign(parser.currData(), parser.currDataSize());
		} else if (naluType == NaluType::PPS) {
			// Update PPS
			mPPS.assign(parser.currData(), parser.currDataSize());
		} else if (naluType == NaluType::KeyFrame) {
			// Send codec specific data first as a STAP-A
			// https://datatracker.ietf.org/doc/html/rfc6184#section-5.7.1
			if (!mSPS.empty() && !mPPS.empty() && !addedParameters) {
				const uint8_t nri = std::max(mSPS.data()[0] & 0x60, mPPS.data()[0] & 0x60);

				ByteBuffer payload;
				ByteWriter writer(payload);

				// nri is already shifted left
				writer.writeU8(nri | STAP_A);

				writer.writeU16(static_cast<uint16_t>(mSPS.size()));
				writer.write(mSPS.data(), mSPS.size());

				writer.writeU16(static_cast<uint16_t>(mPPS.size()));
				writer.write(mPPS.data(), mPPS.size());

				RtpExtension extension = buildExtension(track, simulcast, twcc, true, 0);

				const auto [rollover, sequence] = packetSource->getNextSequence();
				result.push_back(std::make_shared<RtpPacket>(
					track, false, rollover, sequence, frameTimestamp, 0, std::move(extension), std::move(payload)));
			}

			addedParameters = true;
		}

		if (naluType == NaluType::SEI || naluType == NaluType::KeyFrame || naluType == NaluType::NonKeyFrame) {
			// Now the frame itself
			const auto naluDataPtr = parser.currData();
			const auto naluDataSize = parser.currDataSize();

			uint8_t padding = getPadding(track, simulcast, twcc, naluDataSize);
			RtpExtension extension = buildExtension(track, simulcast, twcc, naluType == NaluType::KeyFrame, 0);

			auto basicPacketSize = RtpPacket::kMaxPayloadSize - RtpPacket::kHeaderSize - mediaProtectionOverhead;
			auto packetSize = adjustPacketSize(basicPacketSize, padding, extension);

			if (packetSize >= naluDataSize) {
				// https://datatracker.ietf.org/doc/html/rfc6184#section-5.6
				const auto marker = parser.isAtEnd();
				const auto [rollover, sequence] = packetSource->getNextSequence();
				auto payload = ByteBuffer{ naluDataPtr, naluDataSize };
				result.push_back(
					extension.empty()
						? std::make_shared<RtpPacket>(
							  track, marker, rollover, sequence, frameTimestamp, padding, std::move(payload))
						: std::make_shared<RtpPacket>(track,
													  marker,
													  rollover,
													  sequence,
													  frameTimestamp,
													  padding,
													  std::move(extension),
													  std::move(payload)));
			} else {
				// https://datatracker.ietf.org/doc/html/rfc6184#section-5.8
				const auto nri = static_cast<uint8_t>(naluDataPtr[0] & 0x60);

				// The "+1" is to skip the NALU type
				auto dataPtr = naluDataPtr + 1;
				auto dataSize = naluDataSize - 1;

				auto packetNumber = 0;
				while (dataSize > 0) {
					const auto [rollover, sequence] = packetSource->getNextSequence();

					if (packetNumber > 0) {
						padding = getPadding(track, simulcast, twcc, naluDataSize);
						extension =
							buildExtension(track, simulcast, twcc, naluType == NaluType::KeyFrame, packetNumber);
					}

					packetSize = adjustPacketSize(basicPacketSize, padding, extension) - 2 /*  FU_A headers */;
					if (packetNumber == 0 && packetSize >= dataSize) {
						// The frame now fits in one packet, but a FU-A cannot
						// have both start and end
						packetSize = dataSize - 10;
					}

					ByteBuffer payload;
					ByteWriter writer(payload);

					// nri is already shifted left
					const uint8_t fuIndicator = nri | FU_A;
					writer.writeU8(fuIndicator);

					const auto isStart = packetNumber == 0;
					const auto isEnd = dataSize <= packetSize;
					const uint8_t fuHeader =
						(isStart ? (1 << 7) : 0) | (isEnd ? (1 << 6) : 0) | static_cast<uint8_t>(naluType);
					writer.writeU8(fuHeader);

					const auto marker = isEnd && parser.isAtEnd();

					const auto writeNow = std::min(dataSize, packetSize);
					writer.write(dataPtr, writeNow);

					if (isEnd && writeNow < padding + 100) {
						// padding = 0;
					}

					result.push_back(
						extension.empty()
							? std::make_shared<RtpPacket>(
								  track, marker, rollover, sequence, frameTimestamp, padding, std::move(payload))
							: std::make_shared<RtpPacket>(track,
														  marker,
														  rollover,
														  sequence,
														  frameTimestamp,
														  padding,
														  std::move(extension),
														  std::move(payload)));

					dataPtr += writeNow;
					dataSize -= writeNow;
					packetNumber += 1;
				}
			}
		}
	}

	return result;
}

} // namespace srtc

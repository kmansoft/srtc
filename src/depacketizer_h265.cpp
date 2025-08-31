#include "srtc/depacketizer_h265.h"
#include "srtc/codec_h265.h"
#include "srtc/logging.h"
#include "srtc/util.h"

#define LOG(level, ...) srtc::log(level, "Depacketizer_H265", __VA_ARGS__)

namespace
{

constexpr auto kHaveVPS = 0x01u;
constexpr auto kHaveSPS = 0x02u;
constexpr auto kHavePPS = 0x04u;
constexpr auto kHaveKey = 0x10u;

constexpr auto kHaveAll = kHaveVPS | kHaveSPS | kHavePPS | kHaveKey;

const uint8_t kAnnexB[4] = { 0, 0, 0, 1 };

bool isFrameStartImpl(uint8_t naluType, const uint8_t* data, size_t size)
{
    if (naluType == srtc::h265::NaluType::VPS) {
        // Key frames start with VPS
        return true;
    }
    if (!srtc::h265::isKeyFrameNalu(naluType) && srtc::h265::isSliceNalu(naluType) && size > 0) {
        // Non-key frames start with an indicator bit
        if (srtc::h265::isSliceFrameStart(data, size)) {
            return true;
        }
    }

    return false;
}

} // namespace

namespace srtc
{

DepacketizerH265::DepacketizerH265(const std::shared_ptr<Track>& track)
    : DepacketizerVideo(track)
    , mHaveBits(0)
    , mLastRtpTimestamp(0)
{
}

DepacketizerH265::~DepacketizerH265() = default;

void DepacketizerH265::reset()
{
    mHaveBits = 0;
    mFrameBuffer.clear();
    mLastRtpTimestamp = 0;
}

void DepacketizerH265::extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList)
{
    LOG(SRTC_LOG_Z,
        "----- Frame at %8u, seq = %llu to %llu",
        static_cast<uint32_t>(packetList.front()->rtp_timestamp_ext),
        packetList.front()->seq_ext,
        packetList.back()->seq_ext);

    out.clear();

    std::unique_ptr<ByteBuffer> fu_buf;
    std::unique_ptr<ByteWriter> fu_wrt;

    for (const auto packet : packetList) {
        ByteReader reader(packet->payload);
        if (reader.remaining() >= 2) {
            const auto nalUnitHeader = reader.readU16();
            const auto type = (nalUnitHeader >> 9) & 0x3Fu;
            const auto layerId = (nalUnitHeader >> 3) & 0x3Fu;
            const auto temporalId = nalUnitHeader & 0x7u;

            if (type == h265::kPacket_AP) {
                // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
                while (reader.remaining() >= 2) {
                    const auto size = reader.readU16();
                    if (reader.remaining() >= size && size >= 2) {
                        ByteBuffer buf(packet->payload.data() + reader.position(), size);

                        const auto apHeader = (buf.data()[0] << 8) | buf.data()[1];
                        const auto apNaluType = (apHeader >> 9) & 0x3Fu;
                        LOG(SRTC_LOG_Z, "AP     type = %3u, size = %zu", apNaluType, buf.size());

                        extractImpl(out, packet, std::move(buf));
                        reader.skip(size);
                    } else {
                        break;
                    }
                }
            } else if (type == h265::kPacket_FU) {
                // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.3
                if (reader.remaining() >= 1) {
                    const auto fuHeader = reader.readU8();
                    const auto fuIsStart = (fuHeader & (1 << 7)) != 0;
                    const auto fuIsEnd = (fuHeader & (1 << 6)) != 0;
                    const auto fuType = fuHeader & 0x3Fu;

                    if (fuIsStart) {
                        fu_buf = std::make_unique<ByteBuffer>(fuHeader);
                        fu_wrt = std::make_unique<ByteWriter>(*fu_buf);

                        if (h265::isKeyFrameNalu(fuType)) {
                            std::printf("Starting key frame nalu %u\n", fuType);
                        }

                        fu_wrt->writeU16((fuType << 9) | (layerId << 3) | temporalId);
                    }

                    if (reader.remaining() > 0) {
                        fu_wrt->write(packet->payload.data() + reader.position(), reader.remaining());
                    }

                    if (fuIsEnd && fu_buf) {
                        const auto nalu_type = fuType;
                        LOG(SRTC_LOG_Z, "FU_A   type = %3u, size = %zu", nalu_type, fu_buf->size());

                        extractImpl(out, packet, std::move(*fu_buf));
                    }
                }
            } else if (type <= 40) {
                // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.1

                const auto nalu_type = type;
                LOG(SRTC_LOG_Z, "SINGLE type = %3u, size = %zu", nalu_type, packet->payload.size());

                extractImpl(out, packet, packet->payload.copy());
            }
        }
    }
}

bool DepacketizerH265::isFrameStart(const ByteBuffer& payload) const
{
    ByteReader reader(payload);
    if (reader.remaining() >= 2) {
        const auto nalUnitHeader = reader.readU16();
        const auto type = (nalUnitHeader >> 9) & 0x3Fu;

        if (reader.remaining() < 100) {
            std::printf("Small packet: %8zu, type = %u\n", reader.remaining(), type);
        }

        if (type == h265::kPacket_AP) {
            // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
            while (reader.remaining() >= 2) {
                const auto size = reader.readU16();
                if (reader.remaining() >= size && size >= 2) {
                    const auto apData = payload.data() + reader.position();
                    const auto apHeader = (apData[0] << 8) | apData[1];
                    const auto apNaluType = (apHeader >> 9) & 0x3Fu;

                    if (size > 2 && isFrameStartImpl(apNaluType, apData + 2, size - 2)) {
                        return true;
                    }
                } else {
                    break;
                }
            }
        } else if (type == h265::kPacket_FU) {
            // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.3
            if (reader.remaining() >= 1) {
                const auto fuHeader = reader.readU8();
                const auto fuIsStart = (fuHeader & (1 << 7)) != 0;
                const auto fuNaluType = fuHeader & 0x3Fu;

                const auto fuData = payload.data() + reader.position();
                const auto fuSize = reader.remaining();

                if (fuIsStart) {
                    if (fuSize > 0 && isFrameStartImpl(fuNaluType, fuData, fuSize)) {
                        return true;
                    }
                }
            }
        } else if (type <= 40) {
            // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.1
            const auto data = payload.data() + reader.position();
            const auto size = reader.remaining();

            if (isFrameStartImpl(type, data, size)) {
                return true;
            }
        }
    }

    return false;
}

void DepacketizerH265::extractImpl(std::vector<ByteBuffer>& out, const JitterBufferItem* packet, ByteBuffer&& nalu)
{
    if (nalu.empty()) {
        return;
    }

    if ((mHaveBits & kHaveAll) != kHaveAll) {
        // Wait to emit until we have a key frame
        const auto nalu_type = (nalu.front() >> 1) & 0x3F;
        switch (nalu_type) {
        case h265::NaluType::VPS:
            mHaveBits |= kHaveVPS;
            break;
        case h265::NaluType::SPS:
            mHaveBits |= kHaveSPS;
            break;
        case h265::NaluType::PPS:
            mHaveBits |= kHavePPS;
            break;
        default:
            if (h265::isKeyFrameNalu(nalu_type)) {
                mHaveBits |= kHaveKey;
            } else {
                LOG(SRTC_LOG_V, "Not emitting a non-key frame until there is a keyframe");
                return;
            }
            break;
        }
    }

    if (mLastRtpTimestamp != packet->rtp_timestamp_ext) {
        mLastRtpTimestamp = packet->rtp_timestamp_ext;
        mFrameBuffer.clear();
    }

    mFrameBuffer.append(kAnnexB, sizeof(kAnnexB));
    mFrameBuffer.append(nalu);

    if (packet->marker) {
        out.push_back(std::move(mFrameBuffer));
        mFrameBuffer.clear();
    }
}

} // namespace srtc
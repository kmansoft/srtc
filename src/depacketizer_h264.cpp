#include "srtc/depacketizer_h264.h"
#include "srtc/codec_h264.h"
#include "srtc/logging.h"
#include "srtc/util.h"

#define LOG(level, ...) srtc::log(level, "Depacketizer_H264", __VA_ARGS__)

namespace
{

constexpr auto kHaveSPS = 0x01u;
constexpr auto kHavePPS = 0x02u;
constexpr auto kHaveKey = 0x10u;

constexpr auto kHaveAll = kHaveSPS | kHavePPS | kHaveKey;

const uint8_t kAnnexB[4] = { 0, 0, 0, 1 };

} // namespace

namespace srtc
{

DepacketizerH264::DepacketizerH264(const std::shared_ptr<Track>& track)
    : DepacketizerVideo(track)
    , mHaveBits(0)
    , mLastRtpTimestamp(0)
{
}

DepacketizerH264::~DepacketizerH264() = default;

#if 0
PacketKind DepacketizerH264::getPacketKind(const ByteBuffer& payload, bool marker) const
{
    ByteReader reader(payload);
    if (reader.remaining() >= 1) {
        // https://datatracker.ietf.org/doc/html/rfc6184#section-5.4
        const auto value = reader.readU8();
        const auto type = value & 0x1F;

        if (type == h264::kPacket_STAP_A) {
            // https://datatracker.ietf.org/doc/html/rfc6184#section-5.7.1
            return PacketKind::Standalone;
        } else if (type == h264::kPacket_FU_A) {
            // https://datatracker.ietf.org/doc/html/rfc6184#section-5.8
            if (reader.remaining() >= 1) {
                const auto header = reader.readU8();
                if ((header & (1 << 7)) != 0) {
                    return PacketKind::Start;
                } else if ((header & (1 << 6)) != 0) {
                    return PacketKind::End;
                } else {
                    return PacketKind::Middle;
                }
            }
        } else if (type >= 1 && type <= 23) {
            // https://datatracker.ietf.org/doc/html/rfc6184#section-5.4
            return PacketKind::Standalone;
        }
    }

    return PacketKind::Standalone;
}
#endif

void DepacketizerH264::reset()
{
    mHaveBits = 0;
    mFrameBuffer.clear();
    mLastRtpTimestamp = 0;
}

void DepacketizerH264::extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList)
{
    out.clear();

    std::unique_ptr<ByteBuffer> fu_buf;
    std::unique_ptr<ByteWriter> fu_wrt;

    LOG(SRTC_LOG_Z, "----- Frame at %8u", static_cast<uint32_t>(packetList.front()->rtp_timestamp_ext));

    for (const auto packet : packetList) {
        ByteReader reader(packet->payload);

        if (reader.remaining() > 1) {
            const auto indicator = reader.readU8();

            // https://datatracker.ietf.org/doc/html/rfc6184#section-5.4
            const auto nri = indicator & 0x60u;
            const auto type = indicator & 0x1Fu;

            if (type == h264::kPacket_STAP_A) {
                // https://datatracker.ietf.org/doc/html/rfc6184#section-5.7.1
                while (reader.remaining() >= 2) {
                    const auto size = reader.readU16();
                    if (reader.remaining() >= size && size > 0) {
                        ByteBuffer buf(packet->payload.data() + reader.position(), size);

                        const auto nalu_type = buf.front() & 0x1F;
                        LOG(SRTC_LOG_Z, "STAP_A type = %3u, size = %zu", nalu_type, buf.size());

                        extractImpl(out, packet, std::move(buf));
                        reader.skip(size);
                    } else {
                        break;
                    }
                }
            } else if (type == h264::kPacket_FU_A) {
                // https://datatracker.ietf.org/doc/html/rfc6184#section-5.8
                if (reader.remaining() >= 1) {
                    const auto fuHeader = reader.readU8();
                    const auto fuIsStart = (fuHeader & (1 << 7)) != 0;
                    const auto fuIsEnd = (fuHeader & (1 << 6)) != 0;
                    const auto fuNaluType = fuHeader & 0x1Fu;

                    if (fuIsStart) {
                        fu_buf = std::make_unique<ByteBuffer>(fuHeader);
                        fu_wrt = std::make_unique<ByteWriter>(*fu_buf);
                        fu_wrt->writeU8(nri | fuNaluType);
                    }

                    if (reader.remaining() > 0) {
                        fu_wrt->write(packet->payload.data() + reader.position(), reader.remaining());
                    }

                    if (fuIsEnd) {
                        const auto nalu_type = fu_buf->front() & 0x1F;
                        LOG(SRTC_LOG_Z, "FU_A   type = %3u, size = %zu", nalu_type, fu_buf->size());

                        extractImpl(out, packet, std::move(*fu_buf));
                    }
                }
            } else if (type < 23) {
                // https://datatracker.ietf.org/doc/html/rfc6184#section-5.6

                const auto nalu_type = type;
                LOG(SRTC_LOG_Z, "SINGLE type = %3u, size = %zu", nalu_type, packet->payload.size());

                extractImpl(out, packet, packet->payload.copy());
            }
        }
    }
}

bool DepacketizerH264::isFrameStart(const ByteBuffer& payload) const
{
    // TODO
    return true;
}

void DepacketizerH264::extractImpl(std::vector<ByteBuffer>& out, const JitterBufferItem* packet, ByteBuffer&& nalu)
{
    if (nalu.empty()) {
        return;
    }

    if ((mHaveBits & kHaveAll) != kHaveAll) {
        // Wait to emit until we have a key frame
        const auto type = nalu.front() & 0x1F;
        switch (type) {
        case h264::NaluType::NonKeyFrame:
            LOG(SRTC_LOG_V, "Not emitting a non-key frame until there is a keyframe");
            return;
        case h264::NaluType::SPS:
            mHaveBits |= kHaveSPS;
            break;
        case h264::NaluType::PPS:
            mHaveBits |= kHavePPS;
            break;
        case h264::NaluType::KeyFrame:
            mHaveBits |= kHaveKey;
            break;
        default:
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
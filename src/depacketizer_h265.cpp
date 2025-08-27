#include "srtc/depacketizer_h265.h"
#include "srtc/codec_h265.h"
#include "srtc/logging.h"
#include "srtc/util.h"

#define LOG(level, ...) srtc::log(level, "DepacketizerH265", __VA_ARGS__)

namespace
{

constexpr auto kHaveVPS = 0x01u;
constexpr auto kHaveSPS = 0x02u;
constexpr auto kHavePPS = 0x04u;
constexpr auto kHaveKey = 0x10u;

constexpr auto kHaveAll = kHaveVPS | kHaveSPS | kHavePPS | kHaveKey;

const uint8_t kAnnexB[4] = { 0, 0, 0, 1 };

} // namespace

namespace srtc
{

DepacketizerH265::DepacketizerH265(const std::shared_ptr<Track>& track)
    : Depacketizer(track)
    , mHaveBits(0)
    , mLastRtpTimestamp(0)
{
}

DepacketizerH265::~DepacketizerH265() = default;

PacketKind DepacketizerH265::getPacketKind(const ByteBuffer& payload, bool marker) const
{
    ByteReader reader(payload);
    if (reader.remaining() >= 1) {
        // https://datatracker.ietf.org/doc/html/rfc7798#section-1.1.4
        const auto value = reader.readU8();
        const auto nalu_type = (value >> 1) & 0x3F;

        if (nalu_type == h265::kPacket_AP) {
            // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
            return PacketKind::Standalone;
        } else if (nalu_type == h265::kPacket_FU) {
            // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.3
            if (reader.remaining() >= 2) {
                reader.skip(1);
                const auto fuHeader = reader.readU8();
                if ((fuHeader & (1 << 7)) != 0) {
                    return PacketKind::Start;
                } else if ((fuHeader & (1 << 6)) != 0) {
                    return PacketKind::End;
                } else {
                    return PacketKind::Middle;
                }
            }
        } else if (nalu_type >= 0 && nalu_type <= 40) {
            // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.1
            return PacketKind::Standalone;
        }
    }

    return PacketKind::Standalone;
}

void DepacketizerH265::reset()
{
    mHaveBits = 0;
    mFrameBuffer.clear();
    mLastRtpTimestamp = 0;
}

void DepacketizerH265::extract(std::vector<ByteBuffer>& out, const JitterBufferItem* packet)
{
    out.clear();

    ByteReader reader(packet->payload);
    if (reader.remaining() >= 1) {
        // https://datatracker.ietf.org/doc/html/rfc7798#section-1.1.4
        const auto value = reader.readU8();
        const auto nalu_type = (value >> 1) & 0x3F;

        if (nalu_type == h265::kPacket_AP) {
            // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
            if (reader.remaining() >= 1) {
                reader.skip(1);
            } else {
                return;
            }

            while (reader.remaining() >= 2) {
                const auto size = reader.readU16();
                if (reader.remaining() < size) {
                    break;
                }

                ByteBuffer buf(packet->payload.data() + reader.position(), size);
                extractImpl(out, packet, std::move(buf));

                reader.skip(size);
            }
        } else {
            // https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.1
            extractImpl(out, packet, packet->payload.copy());
        }
    }
}

void DepacketizerH265::extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList)
{
    out.clear();

    ByteBuffer buf;
    ByteWriter w(buf);

    for (const auto packet : packetList) {
        ByteReader reader(packet->payload);

        if (reader.remaining() > 2) {
            const auto payloadHeader = reader.readU16();
            const auto fuHeader = reader.readU8();

            uint8_t layerId = (payloadHeader >> 3)  & 0x3F;
            uint8_t temporalId = payloadHeader & 0x07;
            uint8_t nalu_type = fuHeader & 0x3F;

            if (buf.empty()) {
                w.writeU16((nalu_type << 9) | (layerId << 3) | temporalId);
            }

            const auto pos = reader.position();
            w.write(packet->payload.data() + pos, packet->payload.size() - pos);
        }
    }

    extractImpl(out, packetList.back(), std::move(buf));
}

void DepacketizerH265::extractImpl(std::vector<ByteBuffer>& out, const JitterBufferItem* packet, ByteBuffer&& frame)
{
    if (frame.empty()) {
        return;
    }

    if ((mHaveBits & kHaveAll) != kHaveAll) {
        // Wait to emit until we have a key frame
        const auto nalu_type = (frame.front() >> 1) & 0x3F;
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
    mFrameBuffer.append(frame);

    if (packet->marker) {
        out.push_back(std::move(mFrameBuffer));
        mFrameBuffer.clear();
    }
}

} // namespace srtc
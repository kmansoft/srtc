#include "srtc/depacketizer_h264.h"
#include "srtc/h264.h"
#include "srtc/logging.h"
#include "srtc/util.h"

#include <cassert>

#define LOG(level, ...) srtc::log(level, "DepacketizerH264", __VA_ARGS__)

namespace
{

constexpr auto kHaveSPS = 0x01u;
constexpr auto kHavePPS = 0x02u;
constexpr auto kHaveKey = 0x04u;

constexpr auto kHaveAll = kHaveSPS | kHavePPS | kHaveKey;

const uint8_t kAnnexB[4] = { 0, 0, 0, 1 };

} // namespace

namespace srtc
{

DepacketizerH264::DepacketizerH264(const std::shared_ptr<Track>& track)
    : Depacketizer(track)
    , mHaveBits(0)
    , mLastRtpTimestamp(0)
{
}

DepacketizerH264::~DepacketizerH264() = default;

PacketKind DepacketizerH264::getPacketKind(const ByteBuffer& packet)
{
    ByteReader reader(packet);
    if (reader.remaining() >= 1) {
        // https://datatracker.ietf.org/doc/html/rfc6184#section-5.4
        const auto value = reader.readU8();
        const auto type = value & 0x1F;

        if (type == h264::STAP_A) {
            // https://datatracker.ietf.org/doc/html/rfc6184#section-5.7.1
            return PacketKind::Standalone;
        } else if (type == h264::FU_A) {
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

void DepacketizerH264::reset()
{
    mHaveBits = 0;
    mFrameBuffer.clear();
    mLastRtpTimestamp = 0;
}

void DepacketizerH264::extract(std::vector<ByteBuffer>& out, const JitterBufferItem* packet)
{
    out.clear();

    ByteReader reader(packet->payload);
    if (reader.remaining() >= 1) {
        // https://datatracker.ietf.org/doc/html/rfc6184#section-5.4
        const auto value = reader.readU8();
        const auto type = value & 0x1F;

        if (type == h264::STAP_A) {
            // https://datatracker.ietf.org/doc/html/rfc6184#section-5.7.1
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
            // https://datatracker.ietf.org/doc/html/rfc6184#section-5.4
            extractImpl(out, packet, packet->payload.copy());
        }
    }
}

void DepacketizerH264::extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList)
{
    out.clear();

#ifdef NDEBUG
#else
    assert(!packetList.empty());
    assert(getPacketKind(packetList.front()->payload) == PacketKind::Start);
    for (size_t i = 1; i < packetList.size() - 1; i += 1) {
        assert(getPacketKind(packetList[i]->payload) == PacketKind::Middle);
    }
    assert(getPacketKind(packetList.back()->payload) == PacketKind::End);
#endif

    ByteBuffer buf;
    ByteWriter w(buf);

    for (const auto packet : packetList) {
        ByteReader reader(packet->payload);

        if (reader.remaining() > 2) {
            const auto indicator = reader.readU8();
            const auto header = reader.readU8();

            const auto nri = indicator & 0x60;
            const auto type = header & 0x1F;

            if (buf.empty()) {
                w.writeU8(nri | type);
            }

            const auto pos = reader.position();
            w.write(packet->payload.data() + pos, packet->payload.size() - pos);
        }
    }

    extractImpl(out, packetList.back(), std::move(buf));
}

void DepacketizerH264::extractImpl(std::vector<ByteBuffer>& out, const JitterBufferItem* packet, ByteBuffer&& frame)
{
    if (frame.empty()) {
        return;
    }

    if ((mHaveBits & kHaveAll) != kHaveAll) {
        // Wait to emit until we have a key frame
        const auto type = static_cast<h264::NaluType>(frame.front() & 0x1F);
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
    mFrameBuffer.append(frame);

    if (packet->marker) {
        const auto dump = bin_to_hex(mFrameBuffer.data(), std::min<size_t>(mFrameBuffer.size(), 64));
        std::printf("H264 Out = %s\n", dump.c_str());

        out.push_back(std::move(mFrameBuffer));
        mFrameBuffer.clear();
    }
}

} // namespace srtc
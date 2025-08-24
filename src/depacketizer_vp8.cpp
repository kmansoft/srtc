#include "srtc/depacketizer_vp8.h"

#include "srtc/logging.h"
#include "srtc/util.h"

#define LOG(level, ...) srtc::log(level, "DepacketizerVP8", __VA_ARGS__)

namespace
{

bool isKeyFrame(const srtc::ByteBuffer& frame)
{
    const auto frameData = frame.data();
    const auto frameSize = frame.size();

    if (frameSize < 3) {
        return false;
    }

    // https://datatracker.ietf.org/doc/html/rfc6386#section-9.1
    const auto tag = frameData[0] | (frameData[1] << 8) | (frameData[2] << 16);
    const auto tagFrameType = tag & 0x01;

    return tagFrameType == 0;
}

bool extractPayload(const srtc::ByteBuffer& frame, const uint8_t*& outPayloadData, size_t& outPayloadSize)
{
    // https://datatracker.ietf.org/doc/html/rfc7741#section-4.2

    srtc::ByteReader reader(frame);
    if (reader.remaining() < 1) {
        return false;
    }

    const auto byteHeader = reader.readU8();
    const auto flagHeaderX = byteHeader & (1 << 7);

    if (flagHeaderX) {
        if (reader.remaining() < 1) {
            return false;
        }

        const auto byteExtensionFlags = reader.readU8();
        const auto flagExtensionI = byteExtensionFlags & (1 << 7);
        const auto flagExtensionL = byteExtensionFlags & (1 << 6);
        const auto flagExtensionT = byteExtensionFlags & (1 << 5);
        const auto flagExtensionK = byteExtensionFlags & (1 << 4);

        if (flagExtensionI) {
            if (reader.remaining() < 1) {
                return false;
            }

            const auto bytePictureId0 = reader.readU8();
            if ((bytePictureId0 & (1 << 7)) != 0) {
                if (reader.remaining() < 1) {
                    return false;
                }
                (void)reader.readU8();
            }
        }

        if (flagExtensionL) {
            if (reader.remaining() < 1) {
                return false;
            }
            (void)reader.readU8();
        }

        if (flagExtensionT || flagExtensionK) {
            if (reader.remaining() < 1) {
                return false;
            }
            (void)reader.readU8();
        }
    }

    outPayloadData = frame.data() + reader.position();
    outPayloadSize = reader.remaining();

    return true;
}

} // namespace

namespace srtc
{

DepacketizerVP8::DepacketizerVP8(const std::shared_ptr<Track>& track)
    : Depacketizer(track)
    , mSeenKeyFrame(false)
{
}

DepacketizerVP8::~DepacketizerVP8() = default;

PacketKind DepacketizerVP8::getPacketKind(const JitterBufferItem* packet) const
{
    // https://datatracker.ietf.org/doc/html/rfc7741#section-4.2

    // |X|R|N|S|R| PID |

    const auto data = packet->payload.data();
    const auto size = packet->payload.size();

    if (size >= 1) {
        const auto firstByte = data[0];
        const auto start = (firstByte & (1 << 4)) != 0;
        const auto pid = firstByte & 0x07;

        if (start && pid == 0) {
            if (packet->marker) {
                return PacketKind::Standalone;
            }
            return PacketKind::Start;
        } else if (packet->marker) {
            return PacketKind::End;
        } else {
            return PacketKind::Middle;
        }
    }

    return PacketKind::Standalone;
}

void DepacketizerVP8::reset()
{
    mSeenKeyFrame = false;
}

void DepacketizerVP8::extract(std::vector<ByteBuffer>& out, const JitterBufferItem* packet)
{
    out.clear();

    ByteBuffer buf;
    ByteWriter w(buf);

    const uint8_t* payloadData = nullptr;
    size_t payloadSize = 0;
    if (!extractPayload(packet->payload, payloadData, payloadSize)) {
        return;
    }
    w.write(payloadData, payloadSize);

    extractImpl(out, packet, std::move(buf));
}

void DepacketizerVP8::extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList)
{
    out.clear();

    ByteBuffer buf;
    ByteWriter w(buf);

    for (const auto packet : packetList) {
        const uint8_t* payloadData = nullptr;
        size_t payloadSize = 0;
        if (!extractPayload(packet->payload, payloadData, payloadSize)) {
            return;
        }
        w.write(payloadData, payloadSize);
    }

    extractImpl(out, packetList.back(), std::move(buf));
}

void DepacketizerVP8::extractImpl(std::vector<ByteBuffer>& out, const JitterBufferItem* packet, ByteBuffer&& frame)
{
    if (frame.empty()) {
        return;
    }

    if (!mSeenKeyFrame) {
        if (isKeyFrame(frame)) {
            mSeenKeyFrame = true;
        } else {
            LOG(SRTC_LOG_V, "Not emitting a non-key frame until there is a keyframe");
            return;
        }
    }

    if (packet->marker) {
        out.push_back(std::move(frame));
    }
}

} // namespace srtc
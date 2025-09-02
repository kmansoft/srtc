#include "srtc/depacketizer_av1.h"

#include "srtc/codec_av1.h"
#include "srtc/logging.h"
#include "srtc/util.h"

#define LOG(level, ...) srtc::log(level, "DepacketizerAV1", __VA_ARGS__)

namespace
{

// Chrome sends AV1 data without the size in the OBUs, which kind of makes sense because they can save one LEB128 per
// OBU. When we produce our output, we have to write OBUs with a size, and for that, we have to buffer each OBU's data
// until we are ready to emit and then we know its size (a new OBU starts, or we reach the end of the packet list).

class BufferedObu
{
public:
    BufferedObu();
    ~BufferedObu() = default;

    void setHeader(uint8_t header);
    void setExtension(uint8_t extension);

    void append(const uint8_t* data, size_t size);

    void flushTo(srtc::ByteWriter& out);

private:
    uint8_t mHeader; // or 0 since OBU types start at 1
    uint8_t mExtension;
    srtc::ByteBuffer mBuf;
};

BufferedObu::BufferedObu()
    : mHeader(0)
    , mExtension(0)
{
}

void BufferedObu::setHeader(uint8_t header)
{
    mHeader = header;
}

void BufferedObu::setExtension(uint8_t extension)
{
    mExtension = extension;
}

void BufferedObu::append(const uint8_t* data, size_t size)
{
    mBuf.append(data, size);
}

void BufferedObu::flushTo(srtc::ByteWriter& out)
{
    if (mHeader != 0) {
        out.writeU8(mHeader | (1 << 1)); // force has_size
        if ((mHeader & (1 << 2)) != 0) {
            // Extension
            out.writeU8(mExtension);
        }

        out.writeLEB128(mBuf.size());
        out.write(mBuf);

        mBuf.clear();
        mHeader = 0;
        mExtension = 0;
    }
}

} // namespace

namespace srtc
{

DepacketizerAV1::DepacketizerAV1(const std::shared_ptr<Track>& track)
    : DepacketizerVideo(track)
    , mSeenNewSequence(false)
{
}

DepacketizerAV1::~DepacketizerAV1() = default;

void DepacketizerAV1::reset()
{
    mSeenNewSequence = false;
}

void DepacketizerAV1::extract(std::vector<ByteBuffer>& out, const std::vector<const JitterBufferItem*>& packetList)
{
    out.clear();

    ByteBuffer buf;
    ByteWriter w(buf);

    BufferedObu bufferedObu;

    for (const auto packet : packetList) {
        srtc::ByteReader reader(packet->payload);

        if (reader.remaining() >= 1) {
            // https://aomediacodec.github.io/av1-rtp-spec/#aggregation-header
            // |Z|Y|WW|N|-|-|-|
            const auto packetHeader = reader.readU8();
            const auto valueZ = (packetHeader & (1 << 7)) != 0;
            const auto valueW = (packetHeader >> 4) & 0x03u;
            const auto valueN = (packetHeader >> 3) & 0x01u;

            if (valueN) {
                mSeenNewSequence = true;
            }

            auto obuIndex = 0u;

            while (reader.remaining() >= 1) {
                // Size unless it's the last OBU and we are told there is no size
                size_t obuSize;
                if (valueW > 0 && obuIndex == valueW - 1) {
                    obuSize = reader.remaining();
                } else {
                    obuSize = reader.readLEB128();
                }

                if (reader.remaining() >= obuSize && obuSize > 0) {
                    if (!valueZ) {
                        // Start of a new OBU
                        bufferedObu.flushTo(w);

                        const auto obuHeader = reader.readU8();
                        obuSize -= 1;

                        const auto obuHasExtension = (obuHeader >> 2) & 0x01;

                        bufferedObu.setHeader(obuHeader);
                        if (obuHasExtension && reader.remaining() >= 1) {
                            const auto obuExtension = reader.readU8();
                            obuSize -= 1;

                            bufferedObu.setExtension(obuExtension);
                        }
                    }

                    bufferedObu.append(packet->payload.data() + reader.position(), obuSize);
                    reader.skip(obuSize);
                } else {
                    break;
                }

                if (valueW > 0 && obuIndex == valueW - 1) {
                    // Reached the last one
                    break;
                }

                obuIndex += 1;
            }
        }
    }

    // Flush
    bufferedObu.flushTo(w);

    if (!buf.empty()) {
        extractImpl(out, packetList.back(), std::move(buf));
    }
}

bool DepacketizerAV1::isFrameStart(const ByteBuffer& payload) const
{
    ByteReader reader(payload);

    if (reader.remaining() >= 1) {
        // https://aomediacodec.github.io/av1-rtp-spec/#aggregation-header
        // |Z|Y|WW|N|-|-|-|
        const auto packetHeader = reader.readU8();
        const auto valueZ = (packetHeader & (1 << 7)) != 0;
        if (valueZ) {
            return false;
        }

        const auto valueW = (packetHeader >> 4) & 0x03u;
        auto obuIndex = 0u;

        while (reader.remaining() >= 1) {
            // Size unless it's the last OBU and we are told there is no size
            size_t obuSize;
            if (valueW != 0 && obuIndex == valueW - 1) {
                obuSize = reader.remaining();
            } else {
                obuSize = reader.readLEB128();
            }

            if (reader.remaining() >= 1 && obuSize >= 1) {
                // https://aomediacodec.github.io/av1-spec/#obu-header-syntax
                const auto obuHeader = reader.readU8();
                const auto obuType = (obuHeader >> 3) & 0x0Fu;

                if (obuType == av1::ObuType::SequenceHeader) {
                    return true;
                }
                if (av1::isFrameObuType(obuType)) {
                    return true;
                }

                obuSize -= 1;
            }

            if (valueW != 0 && obuIndex == valueW - 1) {
                // Reached the last one
                break;
            }

            // Advance
            reader.skip(obuSize);
            obuIndex += 1;
        }
    }

    return false;
}

void DepacketizerAV1::extractImpl(std::vector<ByteBuffer>& out, const JitterBufferItem* packet, ByteBuffer&& frame)
{
    if (frame.empty()) {
        return;
    }

    if (!mSeenNewSequence) {
        LOG(SRTC_LOG_V, "Not emitting a non-key frame until there is a new sequence");
        return;
    }

    if (packet->marker) {
        out.push_back(std::move(frame));
    }
}

} // namespace srtc
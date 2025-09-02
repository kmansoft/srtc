#include "srtc/depacketizer_av1.h"

#include "srtc/codec_av1.h"
#include "srtc/logging.h"
#include "srtc/util.h"

#define LOG(level, ...) srtc::log(level, "DepacketizerAV1", __VA_ARGS__)

namespace
{

class BufferedObu
{
public:
    BufferedObu();
    ~BufferedObu() = default;

    void setHeader(uint8_t header);
    void setExtension(uint8_t extension);

    void append(const uint8_t* data, size_t size);

    void writeTo(srtc::ByteWriter& out);
    void clear();

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

void BufferedObu::writeTo(srtc::ByteWriter& out)
{
    if (mHeader == 0) {
        return;
    }

    out.writeU8(mHeader | (1 << 1)); // force has_size
    if ((mHeader & (1 << 2)) != 0) {
        // Extension
        out.writeU8(mExtension);
    }

    out.writeLEB128(mBuf.size());
    out.write(mBuf);

    clear();
}

void BufferedObu::clear()
{
    mBuf.clear();
    mHeader = 0;
    mExtension = 0;
}

bool extractHelper(srtc::ByteBuffer& buf, const std::vector<const srtc::JitterBufferItem*>& packetList)
{
    bool seenNewSequence = false;

    srtc::ByteWriter w(buf);

    BufferedObu bufferedObu;

    for (const auto packet : packetList) {
        srtc::ByteReader reader(packet->payload);

        if (reader.remaining() >= 1) {
            // https://aomediacodec.github.io/av1-rtp-spec/#aggregation-header
            // |Z|Y|WW|N|-|-|-|
            const auto packetHeader = reader.readU8();
            const auto valueZ = (packetHeader & (1 << 7)) != 0;
            const auto valueW = (packetHeader >> 4) & 0x03u;
            const auto valueN = (packetHeader >> 3) & 0x01;

            if (valueN) {
                seenNewSequence = true;
            }

            auto obuIndex = 0u;

            while (reader.remaining() >= 1) {
                // Size unless it's the last OBU and we are told there is no size
                size_t obuSize;
                if (valueW != 0 && obuIndex == valueW - 1) {
                    obuSize = reader.remaining();
                } else {
                    obuSize = reader.readLEB128();
                }

                if (reader.remaining() >= obuSize && obuSize > 0) {
                    if (!valueZ) {
                        // Start of a new OBU
                        bufferedObu.writeTo(w);

                        const auto obuHeader = reader.readU8();
                        obuSize -= 1;

                        const auto obuType = (obuHeader >> 3) & 0x3F;
                        const auto obuHasExtension = (obuHeader >> 2) & 0x01;
                        const auto obuHasSize = (obuHeader >> 1) & 0x01;

                        if (obuType == srtc::av1::ObuType::SequenceHeader) {
                            std::printf("SUB AV1 extract helper: sequence header, has_size = %d\n", obuHasSize);
                        }

                        bufferedObu.setHeader(obuHeader);
                        if (obuHasExtension && reader.remaining() >= 1) {
                            const auto obuExtension = reader.readU8();
                            bufferedObu.setExtension(obuExtension);
                            obuSize -= 1;
                        }
                    }

                    bufferedObu.append(packet->payload.data() + reader.position(), obuSize);
                    reader.skip(obuSize);
                } else {
                    break;
                }

                if (valueW != 0 && obuIndex == valueW - 1) {
                    // Reached last one
                    break;
                }

                obuIndex += 1;
            }
        }
    }

    // Flush
    bufferedObu.writeTo(w);

    return seenNewSequence;
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
    if (extractHelper(buf, packetList)) {
        mSeenNewSequence = true;
    }

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
        const auto flagZ = (packetHeader & (1 << 7)) != 0;
        if (flagZ) {
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
                // Reached last one
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
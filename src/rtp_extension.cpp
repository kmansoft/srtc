#include "srtc/rtp_extension.h"

#include <cassert>
#include <memory>

namespace srtc
{

RtpExtension::RtpExtension()
    : mId(0)
{
}

RtpExtension::RtpExtension(uint16_t id, ByteBuffer&& data)
    : mId(id)
    , mData(std::move(data))
{
}

RtpExtension::RtpExtension(RtpExtension&& source) noexcept
    : mId(source.mId)
    , mData(std::move(source.mData))
{
    source.mId = 0;
}

RtpExtension& RtpExtension::operator=(RtpExtension&& source) noexcept
{
    if (this != &source) {
        mId = source.mId;
        mData = std::move(source.mData);

        source.mId = 0;
    }

    return *this;
}

void RtpExtension::clear()
{
    mId = 0;
    mData.clear();
}

bool RtpExtension::empty() const
{
    return mId == 0 || mData.empty();
}

size_t RtpExtension::size() const
{
    if (empty()) {
        return 0;
    }

    return 2 /* extension id */ + 2 /* extension length */ + 4 * ((mData.size() + 3) / 4);
}

uint16_t RtpExtension::getId() const
{
    return mId;
}

const ByteBuffer& RtpExtension::getData() const
{
    return mData;
}

std::optional<uint16_t> RtpExtension::findU16(uint8_t nExtId) const
{
    if (!empty()) {
        assert(mId == kTwoByte);

        ByteReader reader(mData);
        while (reader.remaining() >= 2) {
            const auto id = reader.readU8();
            const auto len = reader.readU8();
            if (reader.remaining() < len) {
                break;
            }
            if (id == nExtId) {
                if (len < 2) {
                    break;
                }
                return reader.readU16();
            }
            reader.skip(len);
        }
    }

    return {};
}

std::optional<uint32_t> RtpExtension::findU32(uint8_t nExtId) const
{
    if (!empty()) {
        assert(mId == kTwoByte);

        ByteReader reader(mData);
        while (reader.remaining() >= 2) {
            const auto id = reader.readU8();
            const auto len = reader.readU8();
            if (reader.remaining() < len) {
                break;
            }
            if (id == nExtId) {
                if (len < 4) {
                    break;
                }
                return reader.readU32();
            }
            reader.skip(len);
        }
    }

    return {};
}

std::optional<uint64_t> RtpExtension::findU64(uint8_t nExtId) const
{
    if (!empty()) {
        assert(mId == kTwoByte);

        ByteReader reader(mData);
        while (reader.remaining() >= 2) {
            const auto id = reader.readU8();
            const auto len = reader.readU8();
            if (reader.remaining() < len) {
                break;
            }
            if (id == nExtId) {
                if (len < 8) {
                    break;
                }
                return reader.readU64();
            }
            reader.skip(len);
        }
    }

    return {};
}

std::optional<RtpExtension::Value> RtpExtension::findAny(uint8_t nExtId) const
{
    if (!empty()) {
        assert(mId == kTwoByte);

        ByteReader reader(mData);
        while (reader.remaining() >= 2) {
            const auto id = reader.readU8();
            const auto len = reader.readU8();
            if (reader.remaining() < len) {
                break;
            }
            if (id == nExtId) {
                return Value{ reader.current(), len };
            }
            reader.skip(len);
        }
    }

    return {};
}

RtpExtension RtpExtension::copy() const
{
    return { mId, mData.copy() };
}

ByteBuffer RtpExtension::convertOneToTwoByte(const ByteBuffer& src)
{
    ByteBuffer buf;
    ByteWriter writer(buf);

    ByteReader reader(src);

    while (reader.remaining() > 1) {
        const auto value = reader.readU8();
        if (value == 0) {
            break;
        }

        const auto id = static_cast<uint8_t>(value >> 4);
        if (id == 0x0F) {
            break;
        }

        const auto len = static_cast<uint8_t>(value & 0x0Fu) + 1u;
        if (reader.remaining() < len) {
            break;
        }

        uint8_t extbuf[16];
        reader.read(extbuf, len);

        writer.writeU8(id);
        writer.writeU8(static_cast<uint8_t>(len));
        writer.write(extbuf, len);
    }

    return buf;
}

} // namespace srtc

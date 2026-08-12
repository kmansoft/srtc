#include "srtc/rtp_extension.h"

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

bool RtpExtension::isValidExtensionId(uint16_t id)
{
    return id == kOneByte || (id & 0xFFF0u) == kTwoByte;
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
    const auto value = findAny(nExtId);
    if (value.has_value() && value->size >= 2) {
        ByteReader reader(value->ptr, value->size);
        return reader.readU16();
    }

    return {};
}

std::optional<uint32_t> RtpExtension::findU32(uint8_t nExtId) const
{
    const auto value = findAny(nExtId);
    if (value.has_value() && value->size >= 4) {
        ByteReader reader(value->ptr, value->size);
        return reader.readU32();
    }

    return {};
}

std::optional<uint64_t> RtpExtension::findU64(uint8_t nExtId) const
{
    const auto value = findAny(nExtId);
    if (value.has_value() && value->size >= 8) {
        ByteReader reader(value->ptr, value->size);
        return reader.readU64();
    }

    return {};
}

std::optional<RtpExtension::Value> RtpExtension::findAny(uint8_t nExtId) const
{
    if (!empty()) {
        ByteReader reader(mData);
        Element elem = {};

        while (nextElement(reader, elem)) {
            if (elem.id == nExtId) {
                return Value{ elem.ptr, elem.len };
            }
        }
    }

    return {};
}

void RtpExtension::trimPadding()
{
    if (empty()) {
        return;
    }

    ByteReader reader(mData);
    Element elem = {};

    size_t end = 0;
    while (nextElement(reader, elem)) {
        end = reader.position();
    }

    mData.resize(end);
}

RtpExtension RtpExtension::copy() const
{
    if (empty()) {
        return {};
    }

    return { mId, mData.copy() };
}

bool RtpExtension::nextElement(ByteReader& reader, Element& out) const
{
    if (!isValidExtensionId(mId)) {
        return false;
    }

    if (mId == kOneByte) {
        if (reader.remaining() < 1) {
            return false;
        }

        const auto value = reader.readU8();
        if (value == 0) {
            // Padding
            return false;
        }

        const auto id = static_cast<uint8_t>(value >> 4);
        if (id == 0x0Fu) {
            // Reserved
            return false;
        }

        const auto len = static_cast<size_t>((value & 0x0Fu) + 1u);
        if (reader.remaining() < len) {
            return false;
        }

        out.id = id;
        out.ptr = reader.current();
        out.len = len;
        reader.skip(len);
        return true;
    }

    // Two-byte format
    if (reader.remaining() < 2) {
        return false;
    }

    const auto id = reader.readU8();
    if (id == 0) {
        // Padding
        return false;
    }

    const auto len = reader.readU8();
    if (reader.remaining() < len) {
        return false;
    }

    out.id = id;
    out.ptr = reader.current();
    out.len = len;
    reader.skip(len);
    return true;
}

} // namespace srtc

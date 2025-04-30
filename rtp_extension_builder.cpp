#include "srtc/rtp_extension_builder.h"

#include <algorithm>

namespace srtc {

RtpExtensionBuilder::RtpExtensionBuilder()
    : mWriter(mBuf)
{
}

RtpExtensionBuilder::~RtpExtensionBuilder() = default;

void RtpExtensionBuilder::addStringValue(uint8_t id, const std::string& value)
{
    const auto len = std::min(static_cast<size_t>(255), value.length());

    mWriter.writeU8(id);
    mWriter.writeU8(len);
    mWriter.write(reinterpret_cast<const uint8_t*>(value.data()), len);
}

void RtpExtensionBuilder::addBinaryValue(uint8_t id, const ByteBuffer& buf)
{
    const auto len = buf.size();
    mWriter.writeU8(id);
    mWriter.writeU8(len);
    mWriter.write(buf);
}

RtpExtension RtpExtensionBuilder::build()
{
    if (mBuf.empty()) {
        return { 0, {}};
    }

    return { 0x1000, std::move(mBuf) };
}

RtpExtensionBuilder RtpExtensionBuilder::from(const RtpExtension& extension)
{
    if (extension.empty()) {
        return {};
    }

    return RtpExtensionBuilder{ extension.getData() };
}

bool RtpExtensionBuilder::contains(uint8_t id) const
{
    ByteReader reader(mBuf);
    while (reader.remaining() > 2) {
        const auto extensionId = reader.readU8();
        if (extensionId == id) {
            return true;
        }
        const auto extensionLen = reader.readU8();
        reader.skip(extensionLen);
    }

    return false;
}

RtpExtensionBuilder::RtpExtensionBuilder(const ByteBuffer& buf)
    : mBuf(buf.copy())
    , mWriter(mBuf)
{
}

}

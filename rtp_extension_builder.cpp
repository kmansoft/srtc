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

RtpExtension RtpExtensionBuilder::build()
{
    if (mBuf.empty()) {
        return { 0, {}};
    }

    return { 0x1000, std::move(mBuf) };
}

}

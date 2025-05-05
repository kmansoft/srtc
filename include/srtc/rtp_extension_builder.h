#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/rtp_extension.h"

#include <memory>
#include <string>
#include <vector>

namespace srtc
{

class RtpExtensionBuilder
{
public:
    RtpExtensionBuilder();
    ~RtpExtensionBuilder();

    void addStringValue(uint8_t id, const std::string& value);
    void addBinaryValue(uint8_t id, const ByteBuffer& buf);
    void addU16Value(uint8_t id, uint16_t value);

    void addOrReplaceU16Value(uint8_t id, uint16_t value);

    [[nodiscard]] RtpExtension build();

    [[nodiscard]] static RtpExtensionBuilder from(const RtpExtension& extension);

    [[nodiscard]] bool contains(uint8_t id) const;

private:
    ByteBuffer mBuf;
    ByteWriter mWriter;

    explicit RtpExtensionBuilder(const ByteBuffer& buf);
};

} // namespace srtc

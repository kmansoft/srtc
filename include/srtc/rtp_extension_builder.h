#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/rtp_extension.h"

#include <string>
#include <vector>
#include <memory>

namespace srtc {

class RtpExtensionBuilder {
public:
    RtpExtensionBuilder();
    ~RtpExtensionBuilder();

    void addStringValue(uint8_t id, const std::string& value);
    void addBinaryValue(uint8_t id, const ByteBuffer& buf);

    [[nodiscard]] RtpExtension build();

    [[nodiscard]] static RtpExtensionBuilder from(const RtpExtension& extension);

    [[nodiscard]] bool contains(uint8_t id) const;

private:
    ByteBuffer mBuf;
    ByteWriter mWriter;

    explicit RtpExtensionBuilder(const ByteBuffer& buf);
};

}

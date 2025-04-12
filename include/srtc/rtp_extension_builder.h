#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/rtp_extension.h"

#include <string>

namespace srtc {

class RtpExtensionBuilder {
public:
    RtpExtensionBuilder();
    ~RtpExtensionBuilder();

    void addStringValue(uint8_t id, const std::string& value);

    [[nodiscard]] RtpExtension build();

private:
    ByteBuffer mBuf;
    ByteWriter mWriter;
};

}

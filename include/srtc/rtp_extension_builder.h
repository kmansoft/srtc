#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/rtp_extension.h"
#include "srtc/simulcast_layer.h"

#include <string>
#include <vector>

namespace srtc {

class RtpExtensionBuilder {
public:
    RtpExtensionBuilder();
    ~RtpExtensionBuilder();

    void addStringValue(uint8_t id, const std::string& value);
    void addGoogleVLA(uint8_t id,
                      uint8_t ridId,
                      const std::vector<SimulcastLayer>& list);

    [[nodiscard]] RtpExtension build();

    [[nodiscard]] static RtpExtensionBuilder from(const RtpExtension& extension);

    [[nodiscard]] bool contains(uint8_t id) const;

private:
    ByteBuffer mBuf;
    ByteWriter mWriter;

    explicit RtpExtensionBuilder(const ByteBuffer& buf);
};

}

#pragma once

#include <cstdint>
#include "srtc/byte_buffer.h"

namespace srtc {

class RtpExtension {
public:
    RtpExtension();
    RtpExtension(uint16_t id,
                 ByteBuffer&& data);

    RtpExtension(RtpExtension&& source) noexcept;
    RtpExtension& operator=(RtpExtension&& source) noexcept;

    [[nodiscard]] uint16_t getId() const;
    [[nodiscard]] const ByteBuffer& getData() const;


private:
    uint16_t mId;
    ByteBuffer mData;
};

}
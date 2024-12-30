#include "srtc/util.h"

namespace srtc {

std::string bin_to_hex(const uint8_t *buf,
                       size_t size)
{
    static const char *const ALPHABET = "0123456789abcdef";

    std::string hex;
    for (size_t i = 0; i < size; i += 1) {
        hex += (ALPHABET[(buf[i] >> 4) & 0x0F]);
        hex += (ALPHABET[(buf[i]) & 0x0F]);
        if (i != size - 1) {
            hex += ':';
        }
    }
    return hex;
}

ByteBuffer hex_to_bin(const std::string& hex) {
    ByteBuffer buf;
    ByteWriter writer(buf);

    size_t accumCount = 0;
    uint8_t accumBuf = 0;

    for (const auto ch : hex) {
        int accumNibble = -1;

        if (ch >= '0' && ch <= '9') {
            accumNibble = ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            accumNibble = 10 + ch - 'a';
        } else if (ch >= 'A' && ch <= 'F') {
            accumNibble = 10 + ch - 'A';
        }

        if (accumNibble >= 0) {
            accumCount += 4;
            accumBuf = (accumBuf << 4) | accumNibble;

            if (accumCount == 8) {
                writer.writeU8(accumBuf);

                accumCount = 0;
                accumBuf = 0;
            }
        }
    }

    return std::move(buf);
}

}

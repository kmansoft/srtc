#pragma once

#include "srtc/byte_buffer.h"
#include <string>

namespace srtc
{

struct DataChannelMessage {
    enum class Kind {
        kText,
        kBinary
    };

    Kind kind;
    std::string label;
    std::string text;
    ByteBuffer binary;

    static DataChannelMessage makeText(const std::string& label, std::string&& text);
    static DataChannelMessage makeBinary(const std::string& label, srtc::ByteBuffer&& binary);
};

} // namespace srtc
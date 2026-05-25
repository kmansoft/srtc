#include "srtc/data_channel_message.h"

namespace srtc
{

DataChannelMessage DataChannelMessage::makeText(const std::string& label, std::string&& text)
{
    return DataChannelMessage{ Kind::kText, label, std::move(text)};
}

DataChannelMessage DataChannelMessage::makeBinary(const std::string& label, srtc::ByteBuffer&& binary)
{
    return DataChannelMessage{Kind::kBinary, label, "", std::move(binary)};
}

}
#pragma once

#include <string>

namespace srtc
{

class ByteBuffer;

}

namespace srtc::sctp
{

class SctpSessionListener
{
public:
    virtual ~SctpSessionListener() = default;

    virtual void onSctpSendPacket(const ByteBuffer& packet) = 0;
    virtual void onSctpDataChannelOpen(const std::string& label) = 0;
    virtual void onSctpDataChannelText(const std::string& label, const std::string& text) = 0;
    virtual void onSctpDataChannelBinary(const std::string& label, const ByteBuffer& data) = 0;
    virtual void onSctpDataChannelClose(const std::string& label) = 0;

};

} // namespace srtc::sctp
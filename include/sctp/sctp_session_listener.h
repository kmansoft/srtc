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

};

} // namespace srtc::sctp
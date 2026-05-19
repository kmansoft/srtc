#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace srtc::sctp
{

class SctpSessionListener;

class SctpSession
{
public:
    SctpSession(SctpSessionListener* listener,
                uint16_t localPort,
                uint16_t remotePort,
                uint32_t maxMessageSize,
                bool isSetupActive,
                const std::vector<std::string>& dataChannels);

    void start();

private:
    SctpSessionListener* const mListener;
    const uint16_t mLocalPort;
    const uint16_t mRemotePort;
    const uint32_t mMaxMessageSize;
    const bool mIsSetupActive;
    const std::vector<std::string> mDataChannels;

    uint32_t mInitiateTag;
    uint32_t mInitialTsn;
};

} // namespace srtc::sctp
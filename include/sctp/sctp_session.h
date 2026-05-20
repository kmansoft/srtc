#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/scheduler.h"

#include <cstdint>
#include <string>
#include <vector>

namespace srtc::sctp
{

class SctpSessionListener;

class SctpSession
{
public:
    SctpSession(const std::shared_ptr<RealScheduler>& scheduler,
                SctpSessionListener* listener,
                uint16_t localPort,
                uint16_t remotePort,
                uint32_t maxMessageSize,
                bool isSetupActive,
                const std::vector<std::string>& dataChannels);

    void start();

    void onReceiveData(const ByteBuffer& data);

private:
    SctpSessionListener* const mListener;
    const uint16_t mLocalPort;
    const uint16_t mRemotePort;
    const uint32_t mMaxMessageSize;
    const bool mIsSetupActive;
    const std::vector<std::string> mDataChannels;

    uint32_t mInitiateTag;
    uint32_t mInitialTsn;

    ScopedScheduler mScheduler;
};

} // namespace srtc::sctp
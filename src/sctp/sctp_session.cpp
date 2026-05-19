#include "sctp/sctp_session.h"

#include <algorithm>

namespace srtc::sctp
{

SctpSession::SctpSession(SctpSessionListener* listener,
                         uint16_t localPort,
                         uint16_t remotePort,
                         uint32_t maxMessageSize,
                         bool isSetupActive,
                         const std::vector<std::string>& dataChannels)
    : mListener(listener)
    , mLocalPort(localPort)
    , mRemotePort(remotePort)
    , mMaxMessageSize(maxMessageSize)
    , mIsSetupActive(isSetupActive)
    , mDataChannels(dataChannels)
{
}

void SctpSession::start()
{

}

} // namespace srtc::sctp

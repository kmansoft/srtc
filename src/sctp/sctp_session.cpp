#include "sctp/sctp_session.h"
#include "sctp/sctp_defs.h"
#include "sctp/sctp_message_builder.h"
#include "sctp/sctp_session_listener.h"

#include "srtc/byte_buffer.h"

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
    , mInitiateTag(0xFFFFFFFEu)
    , mInitialTsn(0xFFFFFFFEu)
{
}

void SctpSession::start()
{
    // RFC 4960 §3.3.2: INIT chunk body
    ByteBuffer initBody;
    ByteWriter w(initBody);
    w.writeU32(mInitiateTag);        // Initiate Tag (high for tie-breaking)
    w.writeU32(kInitRwnd);
    w.writeU16(kInitStreams);
    w.writeU16(kInitStreams);
    w.writeU32(mInitialTsn);

    // FORWARD_TSN_SUPPORTED parameter (RFC 3758): type + length only, no value
    w.writeU16(kParamForwardTsnSupported);
    w.writeU16(4);

    // Verification tag is 0 in INIT (RFC 4960 §8.5.1)
    SctpMessageBuilder builder(mLocalPort, mRemotePort, 0);
    builder.addChunk(kChunkInit, 0, initBody);
    auto packet = builder.build();

    mListener->onSctpSendPacket(packet);
}

} // namespace srtc::sctp

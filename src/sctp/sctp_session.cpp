#include "sctp/sctp_session.h"
#include "sctp/sctp_defs.h"
#include "sctp/sctp_packet_builder.h"
#include "sctp/sctp_packet.h"
#include "sctp/sctp_session_listener.h"

#include "srtc/byte_buffer.h"
#include "srtc/util.h"

#include <algorithm>

namespace srtc::sctp
{

SctpSession::SctpSession(const std::shared_ptr<RealScheduler>& scheduler,
                         SctpSessionListener* listener,
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
    , mScheduler(scheduler)
{
}

void SctpSession::start()
{
    // RFC 4960 §3.3.2: INIT chunk body
    ByteBuffer initBody;
    ByteWriter w(initBody);
    w.writeU32(mInitiateTag); // Initiate Tag (high for tie-breaking)
    w.writeU32(kInitRwnd);
    w.writeU16(kInitStreams);
    w.writeU16(kInitStreams);
    w.writeU32(mInitialTsn);

    // FORWARD_TSN_SUPPORTED parameter (RFC 3758): type + length only, no value
    w.writeU16(kParamForwardTsnSupported);
    w.writeU16(4);

    // Verification tag is 0 in INIT (RFC 4960 §8.5.1)
    SctpPacketBuilder builder(mLocalPort, mRemotePort, 0);
    builder.addChunk(kChunkInit, 0, initBody);
    auto packet = builder.build();

    mListener->onSctpSendPacket(packet);
}

void SctpSession::onReceiveData(const ByteBuffer& buf)
{
    const auto packet = SctpPacket::parse(buf.data(), buf.size());
    if (!packet) {
        std::printf("*** SCTP recv: parse failed (%zu bytes)\n", buf.size());
        return;
    }

    std::printf("*** SCTP recv (%zu bytes): src=%u dst=%u verTag=0x%08X\n",
                buf.size(), packet->srcPort(), packet->dstPort(), packet->verificationTag());

    for (const auto& chunk : packet->chunks()) {
        std::printf("  %s (0x%02X) flags=0x%02X size=%zu\n",
                    formatChunkName(chunk.type), chunk.type, chunk.flags, chunk.size);

        // Dump fixed fields and parameters for INIT and INIT ACK
        if ((chunk.type == kChunkInit || chunk.type == kChunkInitAck) && chunk.size >= 16) {
            const uint32_t initiateTag = static_cast<uint32_t>(chunk.data[0])  << 24
                                       | static_cast<uint32_t>(chunk.data[1])  << 16
                                       | static_cast<uint32_t>(chunk.data[2])  << 8
                                       | static_cast<uint32_t>(chunk.data[3]);
            const uint32_t arwnd       = static_cast<uint32_t>(chunk.data[4])  << 24
                                       | static_cast<uint32_t>(chunk.data[5])  << 16
                                       | static_cast<uint32_t>(chunk.data[6])  << 8
                                       | static_cast<uint32_t>(chunk.data[7]);
            const uint16_t outStreams  = static_cast<uint16_t>(chunk.data[8]   << 8 | chunk.data[9]);
            const uint16_t inStreams   = static_cast<uint16_t>(chunk.data[10]  << 8 | chunk.data[11]);
            const uint32_t initialTsn  = static_cast<uint32_t>(chunk.data[12]) << 24
                                       | static_cast<uint32_t>(chunk.data[13]) << 16
                                       | static_cast<uint32_t>(chunk.data[14]) << 8
                                       | static_cast<uint32_t>(chunk.data[15]);

            std::printf("    initiateTag=0x%08X a_rwnd=%u streams=%u/%u tsn=0x%08X\n",
                        initiateTag, arwnd, outStreams, inStreams, initialTsn);

            for (const auto& param : chunk.parseParams(16)) {
                std::printf("    param %s (0x%04X): %zu bytes\n",
                            formatParamName(param.type), param.type, param.size);
            }
        }
    }
}

} // namespace srtc::sctp

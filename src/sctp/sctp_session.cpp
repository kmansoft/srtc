#include "sctp/sctp_session.h"
#include "sctp/sctp_defs.h"
#include "sctp/sctp_packet.h"
#include "sctp/sctp_packet_builder.h"
#include "sctp/sctp_session_listener.h"

#include "srtc/byte_buffer.h"
#include "srtc/srtp_hmac_sha1.h"
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
    , mIsRemoteSetupActive(isSetupActive)
    , mRandom(1, 0xFFFFFFFEu)
    , mState(State::New)
    , mInitiateTag(mRandom.next())
    , mInitialTsn(mRandom.next())
    , mPeerTag(0)
    , mPeerOutStreams(0)
    , mPeerInStreams(0)
    , mPeerRwnd(0)
    , mCurrentTsn(mInitialTsn)
    , mPeerCumulativeTsn(0)
    , mHmacKey{}
    , mScheduler(scheduler)
{
    // isSetupActive means the remote (answer) is setup:active = they are DTLS client = we are passive
    // Passive side uses odd stream IDs, active side uses even (RFC 8832)
    uint16_t nextStreamId = mIsRemoteSetupActive ? 1 : 0;
    for (const auto& label : dataChannels) {
        mDataChannels.emplace_back(label, nextStreamId);
        nextStreamId += 2;
    }

    for (size_t i = 0; i < mHmacKey.size(); i += 4) {
        const uint32_t r = mRandom.next();
        mHmacKey[i] = static_cast<uint8_t>(r);
        mHmacKey[i + 1] = static_cast<uint8_t>(r >> 8);
        mHmacKey[i + 2] = static_cast<uint8_t>(r >> 16);
        mHmacKey[i + 3] = static_cast<uint8_t>(r >> 24);
    }
}

SctpSession::~SctpSession()
{
    for (auto& channel : mDataChannels) {
        srtc::Task::cancelHelper(channel.taskT1Open);
    }
}

void SctpSession::start()
{
    sendInit(0);
}

void SctpSession::sendInit(unsigned iteration)
{
    if (iteration > kMaxInitRetransmits) {
        std::printf("*** SCTP INIT: max retransmits reached, giving up\n");
        return;
    }

    ByteBuffer initBody;
    ByteWriter w(initBody);
    w.writeU32(mInitiateTag);
    w.writeU32(kInitRwnd);
    w.writeU16(kInitStreams);
    w.writeU16(kInitStreams);
    w.writeU32(mInitialTsn);
    w.writeU16(kParamForwardTsnSupported);
    w.writeU16(4);

    // Verification tag is 0 in INIT (RFC 4960 §8.5.1)
    SctpPacketBuilder builder(mLocalPort, mRemotePort, 0);
    builder.addChunk(kChunkInit, 0, initBody);
    auto packet = builder.build();
    mListener->onSctpSendPacket(packet);
    mState = State::CookieWait;

    const auto delay = std::chrono::milliseconds(std::min(kRtoInitialMs * (1u << iteration), kRtoMaxMs));
    mTaskT1Init = mScheduler.submit(delay, __FILE__, __LINE__, [this, iteration] { sendInit(iteration + 1); });
}

void SctpSession::sendCookieEcho(unsigned iteration)
{
    if (iteration > kMaxInitRetransmits) {
        std::printf("*** SCTP COOKIE_ECHO: max retransmits reached, giving up\n");
        return;
    }

    mListener->onSctpSendPacket(mCookieEchoPacket);

    const auto delay = std::chrono::milliseconds(std::min(kRtoInitialMs * (1u << iteration), kRtoMaxMs));
    mTaskT1Cookie = mScheduler.submit(delay, __FILE__, __LINE__, [this, iteration] { sendCookieEcho(iteration + 1); });
}

void SctpSession::onReceiveInit(const SctpPacket::Chunk& chunk)
{
    if (chunk.size < 16)
        return;

    ByteReader r(chunk.data, chunk.size);
    const auto peerInitiateTag = r.readU32();

    // RFC 4960 §5.2.1: silently discard if initiate tags are equal
    if (peerInitiateTag == mInitiateTag)
        return;

    const auto peerRwnd = r.readU32();
    const auto peerOutStreams = r.readU16();
    const auto peerInStreams = r.readU16();
    const auto peerInitialTsn = r.readU32();
    mPeerRwnd = peerRwnd;
    mPeerOutStreams = peerOutStreams;
    mPeerInStreams = peerInStreams;
    mPeerCumulativeTsn = peerInitialTsn - 1;

    // Build State Cookie: kCookieDataSize bytes of fields, then HMAC-SHA1
    // Layout (all big-endian):
    //   offset  0: uint32 createdAt
    //   offset  4: uint32 lifetime
    //   offset  8: uint16 localPort
    //   offset 10: uint16 remotePort
    //   offset 12: uint32 ourInitiateTag
    //   offset 16: uint32 ourInitialTsn
    //   offset 20: uint32 peerInitiateTag
    //   offset 24: uint32 peerInitialTsn
    //   offset 28: uint32 peerRwnd
    //   offset 32: uint16 peerOutStreams
    //   offset 34: uint16 peerInStreams
    //   offset 36: uint8[20] HMAC-SHA1
    ByteBuffer cookieData;
    ByteWriter cw(cookieData);

    const auto nowSecs = getSystemTimeSecs();

    cw.writeU32(nowSecs);
    cw.writeU32(kCookieLifetime);
    cw.writeU16(mLocalPort);
    cw.writeU16(mRemotePort);
    cw.writeU32(mInitiateTag);
    cw.writeU32(mInitialTsn);
    cw.writeU32(peerInitiateTag);
    cw.writeU32(peerInitialTsn);
    cw.writeU32(peerRwnd);
    cw.writeU16(peerOutStreams);
    cw.writeU16(peerInStreams);

    uint8_t hmac[kCookieHmacSize];
    HmacSha1 hmacCtx;
    if (!hmacCtx.reset(mHmacKey.data(), mHmacKey.size()))
        return;
    hmacCtx.update(cookieData.data(), cookieData.size());
    hmacCtx.final(hmac);
    cw.write(hmac, kCookieHmacSize);

    // Build INIT_ACK body
    ByteBuffer ackBody;
    ByteWriter aw(ackBody);
    aw.writeU32(mInitiateTag);
    aw.writeU32(kInitRwnd);
    aw.writeU16(kInitStreams);
    aw.writeU16(kInitStreams);
    aw.writeU32(mInitialTsn);
    aw.writeU16(kParamStateCookie);
    aw.writeU16(static_cast<uint16_t>(4 + cookieData.size()));
    aw.write(cookieData);
    aw.writeU16(kParamForwardTsnSupported);
    aw.writeU16(4);

    SctpPacketBuilder builder(mLocalPort, mRemotePort, peerInitiateTag);
    builder.addChunk(kChunkInitAck, 0, ackBody);
    auto packet = builder.build();

    std::printf("  --> sending INIT_ACK (peer tag=0x%08X)\n", peerInitiateTag);
    mListener->onSctpSendPacket(packet);
}

void SctpSession::onReceiveCookieEcho(const SctpPacket::Chunk& chunk)
{
    if (chunk.size < kCookieTotalSize) {
        std::printf("  --> COOKIE_ECHO too small (%zu bytes)\n", chunk.size);
        return;
    }

    // Verify HMAC-SHA1 over the first kCookieDataSize bytes
    uint8_t expectedHmac[kCookieHmacSize];
    HmacSha1 hmacCtx;
    if (!hmacCtx.reset(mHmacKey.data(), mHmacKey.size()))
        return;
    hmacCtx.update(chunk.data, kCookieDataSize);
    hmacCtx.final(expectedHmac);

    if (!constTimeEqual(chunk.data + kCookieDataSize, expectedHmac, kCookieHmacSize)) {
        std::printf("  --> COOKIE_ECHO HMAC mismatch, discarding\n");
        return;
    }

    // Verify lifetime
    ByteReader r(chunk.data, kCookieTotalSize);
    const auto createdAt = r.readU32();
    const auto lifetime = r.readU32();
    const auto nowSecs = getSystemTimeSecs();
    if (nowSecs > createdAt + lifetime) {
        std::printf("  --> COOKIE_ECHO expired\n");
        return;
    }

    // Read peerInitiateTag from cookie (offset 20)
    r.skip(12); // localPort, remotePort, ourInitiateTag, ourInitialTsn
    const uint32_t peerInitiateTag = r.readU32();

    // Send COOKIE_ACK
    SctpPacketBuilder builder(mLocalPort, mRemotePort, peerInitiateTag);
    builder.addChunk(kChunkCookieAck, 0, nullptr, 0);
    auto packet = builder.build();

    std::printf("  --> sending COOKIE_ACK (peer tag=0x%08X)\n", peerInitiateTag);
    mListener->onSctpSendPacket(packet);
}

void SctpSession::onReceiveData(const ByteBuffer& buf)
{
    const auto packet = SctpPacket::parse(buf.data(), buf.size());
    if (!packet) {
        std::printf("*** SCTP recv: parse failed (%zu bytes)\n", buf.size());
        return;
    }

    // RFC 4960 §8.5: verify verification tag — 0 for INIT, our tag for everything else
    const bool isInit = !packet->chunks().empty() && packet->chunks()[0].type == kChunkInit;
    const uint32_t expectedTag = isInit ? 0 : mInitiateTag;
    if (packet->verificationTag() != expectedTag) {
        std::printf("*** SCTP recv: wrong verification tag 0x%08X (expected 0x%08X), discarding\n",
                    packet->verificationTag(),
                    expectedTag);
        return;
    }

    std::printf("*** SCTP recv (%zu bytes): src=%u dst=%u verTag=0x%08X\n",
                buf.size(),
                packet->srcPort(),
                packet->dstPort(),
                packet->verificationTag());

    for (const auto& chunk : packet->chunks()) {
        std::printf(
            "  %s (0x%02X) flags=0x%02X size=%zu\n", formatChunkName(chunk.type), chunk.type, chunk.flags, chunk.size);

        if (chunk.type == kChunkData) {
            onReceiveDataChunk(chunk);
        }

        if (chunk.type == kChunkInit) {
            onReceiveInit(chunk);
        }

        if (chunk.type == kChunkCookieEcho) {
            onReceiveCookieEcho(chunk);
        }

        if (chunk.type == kChunkCookieAck && mState == State::CookieEchoed) {
            Task::cancelHelper(mTaskT1Cookie);
            mState = State::Established;
            std::printf("  --> SCTP established\n");
            onAssociationEstablished();
        }

        if (chunk.type == kChunkInitAck && mState == State::CookieWait && chunk.size >= 16) {
            ByteReader r(chunk.data, chunk.size);
            mPeerTag = r.readU32();
            mPeerRwnd = r.readU32();
            mPeerOutStreams = r.readU16();
            mPeerInStreams = r.readU16();
            mPeerCumulativeTsn = r.readU32() - 1; // peerInitialTsn - 1

            for (const auto& param : chunk.parseParams(16)) {
                if (param.type == kParamStateCookie) {
                    srtc::Task::cancelHelper(mTaskT1Init);

                    SctpPacketBuilder builder(mLocalPort, mRemotePort, mPeerTag);
                    builder.addChunk(kChunkCookieEcho, 0, param.data, param.size);
                    mCookieEchoPacket = builder.build();

                    mState = State::CookieEchoed;
                    sendCookieEcho(0);
                    break;
                }
            }
        }

        // Dump fixed fields and parameters for INIT and INIT ACK
        if ((chunk.type == kChunkInit || chunk.type == kChunkInitAck) && chunk.size >= 16) {
            ByteReader r(chunk.data, chunk.size);
            const auto initiateTag = r.readU32();
            const auto arwnd = r.readU32();
            const auto outStreams = r.readU16();
            const auto inStreams = r.readU16();
            const auto initialTsn = r.readU32();

            std::printf("    initiateTag=0x%08X a_rwnd=%u streams=%u/%u tsn=0x%08X\n",
                        initiateTag,
                        arwnd,
                        outStreams,
                        inStreams,
                        initialTsn);

            for (const auto& param : chunk.parseParams(16)) {
                std::printf("    param %s (0x%04X): %zu bytes\n", formatParamName(param.type), param.type, param.size);
            }
        }
    }
}

void SctpSession::onAssociationEstablished()
{
    for (auto& channel : mDataChannels) {
        sendDataChannelOpen(channel, 0);
    }
}

void SctpSession::sendDataChannelOpen(DataChannel& channel, unsigned iteration)
{
    if (iteration > kMaxInitRetransmits) {
        std::printf("*** SCTP DATA_CHANNEL_OPEN: max retransmits reached for \"%s\", giving up\n",
                    channel.label.c_str());
        return;
    }

    ByteBuffer payload;
    ByteWriter pw(payload);
    pw.writeU8(kDcepMsgOpen);
    pw.writeU8(kDcepChannelReliable);
    pw.writeU16(kDcepPriorityNormal);
    pw.writeU32(0); // reliability parameter
    pw.writeU16(static_cast<uint16_t>(channel.label.size()));
    pw.writeU16(0); // protocol length
    pw.write(reinterpret_cast<const uint8_t*>(channel.label.data()), channel.label.size());

    ByteBuffer chunkBody;
    ByteWriter cw(chunkBody);
    cw.writeU32(mCurrentTsn++);
    cw.writeU16(channel.streamId);
    cw.writeU16(0); // SSN = 0 for DCEP (RFC 8832)
    cw.writeU32(kPpidDcep);
    cw.write(payload);

    SctpPacketBuilder builder(mLocalPort, mRemotePort, mPeerTag);
    builder.addChunk(kChunkData, kDataFlagComplete, chunkBody);
    auto packet = builder.build();

    std::printf("  --> sending DATA_CHANNEL_OPEN stream=%u label=\"%s\"\n",
                channel.streamId, channel.label.c_str());
    mListener->onSctpSendPacket(packet);
    channel.state = DataChannelState::kOpening;

    const auto delay = std::chrono::milliseconds(std::min(kRtoInitialMs * (1u << iteration), kRtoMaxMs));
    channel.taskT1Open = mScheduler.submit(delay, __FILE__, __LINE__, [this, &channel, iteration] {
        sendDataChannelOpen(channel, iteration + 1);
    });
}

void SctpSession::onReceiveDataChunk(const SctpPacket::Chunk& chunk)
{
    // DATA chunk body: TSN(4) + streamId(2) + SSN(2) + PPID(4) + payload
    if (chunk.size < 12) return;

    ByteReader r(chunk.data, chunk.size);
    const auto tsn      = r.readU32();
    const auto streamId = r.readU16();
    const auto ssn      = r.readU16();
    (void)ssn; // needed for ordered delivery
    const auto ppid     = r.readU32();

    // Advance cumulative TSN if in order
    if (tsn == mPeerCumulativeTsn + 1) {
        mPeerCumulativeTsn = tsn;
    }
    sendSack();

    if (ppid != kPpidDcep || r.remaining() < 1) return;

    const auto msgType = r.readU8();

    if (msgType == kDcepMsgAck) {
        for (auto& channel : mDataChannels) {
            if (channel.streamId == streamId && channel.state == DataChannelState::kOpening) {
                srtc::Task::cancelHelper(channel.taskT1Open);
                channel.state = DataChannelState::kOpen;
                std::printf("  --> DATA_CHANNEL_ACK stream=%u label=\"%s\"\n",
                            streamId, channel.label.c_str());
                mListener->onSctpDataChannelOpen(channel.label);
                break;
            }
        }
    } else if (msgType == kDcepMsgOpen) {
        // Parse label from DATA_CHANNEL_OPEN body
        // channelType(1) + priority(2) + reliabilityParameter(4) + labelLength(2) + protocolLength(2)
        if (r.remaining() < 11) return;
        r.skip(7); // channelType, priority, reliabilityParameter
        const auto labelLen = r.readU16();
        r.skip(2); // protocolLength
        if (labelLen > r.remaining()) return;
        const auto label = r.readString(labelLen);

        // Send ACK
        ByteBuffer chunkBody;
        ByteWriter cw(chunkBody);
        cw.writeU32(mCurrentTsn++);
        cw.writeU16(streamId);
        cw.writeU16(0);
        cw.writeU32(kPpidDcep);
        cw.writeU8(kDcepMsgAck);

        SctpPacketBuilder builder(mLocalPort, mRemotePort, mPeerTag);
        builder.addChunk(kChunkData, kDataFlagComplete, chunkBody);
        auto packet = builder.build();

        std::printf("  --> sending DATA_CHANNEL_ACK stream=%u label=\"%s\"\n", streamId, label.c_str());
        mListener->onSctpSendPacket(packet);
        mListener->onSctpDataChannelOpen(label);
    }
}

void SctpSession::sendSack()
{
    ByteBuffer body;
    ByteWriter w(body);
    w.writeU32(mPeerCumulativeTsn);
    w.writeU32(kInitRwnd);
    w.writeU16(0); // no gap ack blocks
    w.writeU16(0); // no duplicate TSNs

    SctpPacketBuilder builder(mLocalPort, mRemotePort, mPeerTag);
    builder.addChunk(kChunkSack, 0, body);
    auto packet = builder.build();
    mListener->onSctpSendPacket(packet);
}

} // namespace srtc::sctp

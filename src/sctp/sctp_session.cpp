#include "sctp/sctp_session.h"
#include "sctp/sctp_defs.h"
#include "sctp/sctp_packet_builder.h"
#include "sctp/sctp_packet.h"
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
    , mIsSetupActive(isSetupActive)
    , mDataChannels(dataChannels)
    , mRandom(1, 0xFFFFFFFEu)
    , mState(State::New)
    , mInitiateTag(mRandom.next())
    , mInitialTsn(mRandom.next())
    , mPeerTag(0)
    , mHmacKey{}
    , mScheduler(scheduler)
{
    for (size_t i = 0; i < mHmacKey.size(); i += 4) {
        const uint32_t r = mRandom.next();
        mHmacKey[i]     = static_cast<uint8_t>(r);
        mHmacKey[i + 1] = static_cast<uint8_t>(r >> 8);
        mHmacKey[i + 2] = static_cast<uint8_t>(r >> 16);
        mHmacKey[i + 3] = static_cast<uint8_t>(r >> 24);
    }
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
    mState = State::CookieWait;
}

void SctpSession::onReceiveInit(const SctpPacket::Chunk& chunk)
{
    if (chunk.size < 16) return;

    const uint32_t peerInitiateTag = static_cast<uint32_t>(chunk.data[0]) << 24
                                   | static_cast<uint32_t>(chunk.data[1]) << 16
                                   | static_cast<uint32_t>(chunk.data[2]) << 8
                                   | static_cast<uint32_t>(chunk.data[3]);

    // RFC 4960 §5.2.1: silently discard if initiate tags are equal
    if (peerInitiateTag == mInitiateTag) return;

    const uint32_t peerRwnd      = static_cast<uint32_t>(chunk.data[4]) << 24
                                 | static_cast<uint32_t>(chunk.data[5]) << 16
                                 | static_cast<uint32_t>(chunk.data[6]) << 8
                                 | static_cast<uint32_t>(chunk.data[7]);
    const uint16_t peerOutStreams = static_cast<uint16_t>(chunk.data[8]  << 8 | chunk.data[9]);
    const uint16_t peerInStreams  = static_cast<uint16_t>(chunk.data[10] << 8 | chunk.data[11]);
    const uint32_t peerInitialTsn = static_cast<uint32_t>(chunk.data[12]) << 24
                                  | static_cast<uint32_t>(chunk.data[13]) << 16
                                  | static_cast<uint32_t>(chunk.data[14]) << 8
                                  | static_cast<uint32_t>(chunk.data[15]);

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
    if (!hmacCtx.reset(mHmacKey.data(), mHmacKey.size())) return;
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
    if (!hmacCtx.reset(mHmacKey.data(), mHmacKey.size())) return;
    hmacCtx.update(chunk.data, kCookieDataSize);
    hmacCtx.final(expectedHmac);

    if (!constTimeEqual(chunk.data + kCookieDataSize, expectedHmac, kCookieHmacSize)) {
        std::printf("  --> COOKIE_ECHO HMAC mismatch, discarding\n");
        return;
    }

    // Verify lifetime
    ByteReader r(chunk.data, kCookieTotalSize);
    const uint32_t createdAt = r.readU32();
    const uint32_t lifetime  = r.readU32();
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
    const bool isInit = !packet->chunks().empty()
                     && packet->chunks()[0].type == kChunkInit;
    const uint32_t expectedTag = isInit ? 0 : mInitiateTag;
    if (packet->verificationTag() != expectedTag) {
        std::printf("*** SCTP recv: wrong verification tag 0x%08X (expected 0x%08X), discarding\n",
                    packet->verificationTag(), expectedTag);
        return;
    }

    std::printf("*** SCTP recv (%zu bytes): src=%u dst=%u verTag=0x%08X\n",
                buf.size(), packet->srcPort(), packet->dstPort(), packet->verificationTag());

    for (const auto& chunk : packet->chunks()) {
        std::printf("  %s (0x%02X) flags=0x%02X size=%zu\n",
                    formatChunkName(chunk.type), chunk.type, chunk.flags, chunk.size);

        if (chunk.type == kChunkInit) {
            onReceiveInit(chunk);
        }

        if (chunk.type == kChunkCookieEcho) {
            onReceiveCookieEcho(chunk);
        }

        if (chunk.type == kChunkCookieAck && mState == State::CookieEchoed) {
            mState = State::Established;
            std::printf("  --> SCTP established\n");
        }

        if (chunk.type == kChunkInitAck && mState == State::CookieWait && chunk.size >= 16) {
            mPeerTag = static_cast<uint32_t>(chunk.data[0]) << 24
                     | static_cast<uint32_t>(chunk.data[1]) << 16
                     | static_cast<uint32_t>(chunk.data[2]) << 8
                     | static_cast<uint32_t>(chunk.data[3]);

            for (const auto& param : chunk.parseParams(16)) {
                if (param.type == kParamStateCookie) {
                    SctpPacketBuilder builder(mLocalPort, mRemotePort, mPeerTag);
                    builder.addChunk(kChunkCookieEcho, 0, param.data, param.size);
                    auto cookie = builder.build();
                    mListener->onSctpSendPacket(cookie);
                    mState = State::CookieEchoed;
                    break;
                }
            }
        }

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

#include "sctp/sctp_session.h"
#include "sctp/sctp_defs.h"
#include "sctp/sctp_packet.h"
#include "sctp/sctp_packet_builder.h"
#include "sctp/sctp_session_listener.h"

#include "srtc/byte_buffer.h"
#include "srtc/data_channel_message.h"
#include "srtc/logging.h"
#include "srtc/srtp_hmac_sha1.h"
#include "srtc/util.h"

#include <algorithm>

#define LOG(level, ...) srtc::log(level, "PeerConnection", __VA_ARGS__)

namespace
{

std::chrono::milliseconds retransmitDelay(unsigned iteration)
{
    uint32_t delay = srtc::sctp::kRtoInitialMs;
    for (unsigned i = 0; i < iteration; ++i) {
        delay = delay * 3 / 2;
        if (delay >= srtc::sctp::kRtoMaxMs) {
            return std::chrono::milliseconds(srtc::sctp::kRtoMaxMs);
        }
    }
    return std::chrono::milliseconds(delay);
}

} // namespace

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
        mDataChannels.emplace_back(label, nextStreamId, false);
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
        Task::cancelHelper(channel.taskT1Open);

        if (channel.state == DataChannelState::kOpen) {
            mListener->onSctpDataChannelClose(channel.label);
        }
    }
}

void SctpSession::start()
{
    sendInit(0);
}

bool SctpSession::isChannelOpen(const std::string& label) const
{
    return std::any_of(mDataChannels.begin(), mDataChannels.end(), [&label](const DataChannel& channel) {
        return channel.label == label && channel.state == DataChannelState::kOpen;
    });
}

void SctpSession::send(DataChannelMessage&& message)
{
    if (mState != State::Established) {
        return;
    }

    auto it = std::find_if(
        mDataChannels.begin(), mDataChannels.end(), [&](const DataChannel& ch) { return ch.label == message.label; });
    if (it == mDataChannels.end() || it->state != DataChannelState::kOpen) {
        return;
    }

    const size_t payloadSize =
        message.kind == DataChannelMessage::Kind::kText ? message.text.size() : message.binary.size();

    if (payloadSize > 0 && mFlightSize + payloadSize > mPeerRwnd) {
        mPendingSend.emplace_back(std::move(message));
        return;
    }

    sendMessageNow(*it, std::move(message));
}

void SctpSession::sendMessageNow(DataChannel& channel, DataChannelMessage&& message)
{
    std::string textStorage;
    const uint8_t* payload;
    size_t payloadSize;
    uint32_t ppid;

    if (message.kind == DataChannelMessage::Kind::kText) {
        textStorage = std::move(message.text);
        payload = reinterpret_cast<const uint8_t*>(textStorage.data());
        payloadSize = textStorage.size();
        ppid = payloadSize == 0 ? kPpidStringEmpty : kPpidString;
    } else {
        payload = message.binary.data();
        payloadSize = message.binary.size();
        ppid = payloadSize == 0 ? kPpidBinaryEmpty : kPpidBinary;
    }

    const uint16_t ssn = channel.sendSsn++;
    const uint8_t unorderedFlag = channel.unordered ? kDataFlagUnordered : 0;
    const bool wasEmpty = mSentChunks.empty();

    constexpr size_t kMaxFragmentPayload = 1100;

    auto sendFragment = [&](uint8_t flags, const uint8_t* fragPayload, size_t fragSize) {
        const uint32_t tsn = mCurrentTsn++;

        ByteBuffer chunkBody;
        ByteWriter cw(chunkBody);
        cw.writeU32(tsn);
        cw.writeU16(channel.streamId);
        cw.writeU16(ssn);
        cw.writeU32(ppid);
        cw.write(fragPayload, fragSize);

        SctpPacketBuilder builder(mLocalPort, mRemotePort, mPeerTag);
        builder.addChunk(kChunkData, flags, chunkBody);
        mListener->onSctpSendPacket(builder.build());

        mSentChunks.push_back(SentChunk{ tsn, flags, fragSize, std::move(chunkBody) });
        mFlightSize += fragSize;
    };

    if (payloadSize <= kMaxFragmentPayload) {
        sendFragment(static_cast<uint8_t>(kDataFlagComplete | unorderedFlag), payload, payloadSize);
    } else {
        size_t offset = 0;
        bool isFirst = true;
        while (offset < payloadSize) {
            const size_t fragSize = std::min(payloadSize - offset, kMaxFragmentPayload);
            const bool isLast = (offset + fragSize >= payloadSize);
            uint8_t flags = unorderedFlag;
            if (isFirst)
                flags |= kDataFlagB;
            if (isLast)
                flags |= kDataFlagE;
            sendFragment(flags, payload + offset, fragSize);
            offset += fragSize;
            isFirst = false;
        }
    }

    if (wasEmpty) {
        startT3Rtx();
    }
}

void SctpSession::transmitPending()
{
    while (!mPendingSend.empty()) {
        auto& msg = mPendingSend.front();

        auto it = std::find_if(
            mDataChannels.begin(), mDataChannels.end(), [&](const DataChannel& ch) { return ch.label == msg.label; });
        if (it == mDataChannels.end() || it->state != DataChannelState::kOpen) {
            mPendingSend.pop_front();
            continue;
        }

        const size_t payloadSize = msg.kind == DataChannelMessage::Kind::kText ? msg.text.size() : msg.binary.size();
        if (payloadSize > 0 && mFlightSize + payloadSize > mPeerRwnd)
            break;

        sendMessageNow(*it, std::move(msg));
        mPendingSend.pop_front();
    }
}

void SctpSession::sendInit(unsigned iteration)
{
    if (iteration > kMaxInitRetransmits) {
        LOG(SRTC_LOG_E, "SCTP INIT: max retransmits reached, giving up");
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

    const auto delay = retransmitDelay(iteration);
    mTaskT1Init = mScheduler.submit(delay, __FILE__, __LINE__, [this, iteration] { sendInit(iteration + 1); });
}

void SctpSession::sendCookieEcho(unsigned iteration)
{
    if (iteration > kMaxInitRetransmits) {
        LOG(SRTC_LOG_E, "SCTP COOKIE_ECHO: max retransmits reached, giving up");
        return;
    }

    mListener->onSctpSendPacket(mCookieEchoPacket);

    const auto delay = retransmitDelay(iteration);
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

    mListener->onSctpSendPacket(packet);
}

void SctpSession::onReceiveCookieEcho(const SctpPacket::Chunk& chunk)
{
    if (chunk.size < kCookieTotalSize) {
        LOG(SRTC_LOG_W, "COOKIE_ECHO too small (%zu bytes)", chunk.size);
        return;
    }

    // Verify HMAC-SHA1 over the first kCookieDataSize bytes
    uint8_t expectedHmac[kCookieHmacSize];
    HmacSha1 hmacCtx;
    if (!hmacCtx.reset(mHmacKey.data(), mHmacKey.size())) {
        return;
    }
    hmacCtx.update(chunk.data, kCookieDataSize);
    hmacCtx.final(expectedHmac);

    if (!constTimeEqual(chunk.data + kCookieDataSize, expectedHmac, kCookieHmacSize)) {
        LOG(SRTC_LOG_W, "COOKIE_ECHO HMAC mismatch, discarding");
        return;
    }

    // Verify lifetime
    ByteReader r(chunk.data, kCookieTotalSize);
    const auto createdAt = r.readU32();
    const auto lifetime = r.readU32();
    const auto nowSecs = getSystemTimeSecs();
    if (nowSecs > createdAt + lifetime) {
        LOG(SRTC_LOG_W, "COOKIE_ECHO expired");
        return;
    }

    // Read peerInitiateTag from cookie (offset 20)
    r.skip(12); // localPort, remotePort, ourInitiateTag, ourInitialTsn
    const uint32_t peerInitiateTag = r.readU32();

    // Send COOKIE_ACK
    SctpPacketBuilder builder(mLocalPort, mRemotePort, peerInitiateTag);
    builder.addChunk(kChunkCookieAck, 0, nullptr, 0);
    auto packet = builder.build();

    mListener->onSctpSendPacket(packet);
}

void SctpSession::onReceiveData(const ByteBuffer& buf)
{
    const auto packet = SctpPacket::parse(buf.data(), buf.size());
    if (!packet) {
        LOG(SRTC_LOG_W, "recv: parse failed (%zu bytes)", buf.size());
        return;
    }

    // RFC 4960 §8.5: verify verification tag — 0 for INIT, our tag for everything else
    const bool isInit = !packet->chunks().empty() && packet->chunks()[0].type == kChunkInit;
    const uint32_t expectedTag = isInit ? 0 : mInitiateTag;
    if (packet->verificationTag() != expectedTag) {
        LOG(SRTC_LOG_W,
            "recv: wrong verification tag 0x%08X (expected 0x%08X), discarding",
            packet->verificationTag(),
            expectedTag);
        return;
    }

    for (const auto& chunk : packet->chunks()) {
        switch (chunk.type) {
        case kChunkData:
            onReceiveDataChunk(chunk);
            break;
        case kChunkSack:
            onReceiveSack(chunk);
            break;
        case kChunkReconfig:
            onReceiveReconfig(chunk);
            break;
        case kChunkInit:
            onReceiveInit(chunk);
            break;
        case kChunkCookieEcho:
            onReceiveCookieEcho(chunk);
            break;
        case kChunkCookieAck:
            if (mState == State::CookieEchoed) {
                Task::cancelHelper(mTaskT1Cookie);
                mState = State::Established;
                onAssociationEstablished();
            }
            break;
        case kChunkInitAck:
            if (mState == State::CookieWait && chunk.size >= 16) {
                ByteReader r(chunk.data, chunk.size);
                mPeerTag = r.readU32();
                mPeerRwnd = r.readU32();
                mPeerOutStreams = r.readU16();
                mPeerInStreams = r.readU16();
                mPeerCumulativeTsn = r.readU32() - 1; // peerInitialTsn - 1

                for (const auto& param : chunk.parseParams(16)) {
                    if (param.type == kParamStateCookie) {
                        Task::cancelHelper(mTaskT1Init);

                        SctpPacketBuilder builder(mLocalPort, mRemotePort, mPeerTag);
                        builder.addChunk(kChunkCookieEcho, 0, param.data, param.size);
                        mCookieEchoPacket = builder.build();

                        mState = State::CookieEchoed;
                        sendCookieEcho(0);
                        break;
                    }
                }
            }
            break;
        default:
            break;
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
        LOG(SRTC_LOG_W, "DATA_CHANNEL_OPEN: max retransmits reached for \"%s\", giving up", channel.label.c_str());
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

    mListener->onSctpSendPacket(packet);
    channel.state = DataChannelState::kOpening;

    const auto delay = retransmitDelay(iteration);
    channel.taskT1Open = mScheduler.submit(
        delay, __FILE__, __LINE__, [this, &channel, iteration] { sendDataChannelOpen(channel, iteration + 1); });
}

void SctpSession::onReceiveDataChunk(const SctpPacket::Chunk& chunk)
{
    // DATA chunk body: TSN(4) + streamId(2) + SSN(2) + PPID(4) + payload
    if (chunk.size < 12)
        return;

    ByteReader r(chunk.data, chunk.size);
    const auto tsn = r.readU32();
    const auto streamId = r.readU16();
    const auto ssn = r.readU16();
    (void)ssn; // needed for ordered delivery
    const auto ppid = r.readU32();

    // Duplicate detection: TSN already covered by cumulative or seen out-of-order
    const bool isDuplicate = static_cast<int32_t>(tsn - mPeerCumulativeTsn) <= 0 || mPeerOutOfOrderTsns.count(tsn) > 0;
    if (isDuplicate) {
        sendSack();
        return;
    }

    if (tsn == mPeerCumulativeTsn + 1) {
        mPeerCumulativeTsn = tsn;
        // Drain any now-consecutive TSNs from the out-of-order set
        while (true) {
            auto it = mPeerOutOfOrderTsns.find(mPeerCumulativeTsn + 1);
            if (it == mPeerOutOfOrderTsns.end())
                break;
            mPeerCumulativeTsn++;
            mPeerOutOfOrderTsns.erase(it);
        }
    } else {
        mPeerOutOfOrderTsns.insert(tsn);
        if (mPeerOutOfOrderTsns.size() > 1024) {
            mPeerOutOfOrderTsns.erase(mPeerOutOfOrderTsns.begin());
        }
    }
    sendSack();

    if (ppid == kPpidString || ppid == kPpidStringEmpty || ppid == kPpidBinary || ppid == kPpidBinaryEmpty) {
        for (auto& channel : mDataChannels) {
            if (channel.streamId != streamId) {
                continue;
            }
            const auto messages =
                channel.receiveBuffer.receive(ssn, chunk.flags, channel.unordered, ppid, r.current(), r.remaining());
            for (const auto& msg : messages) {
                if (msg.ppid == kPpidString || msg.ppid == kPpidStringEmpty) {
                    const auto text = std::string(reinterpret_cast<const char*>(msg.data.data()), msg.data.size());
                    mListener->onSctpDataChannelText(channel.label, text);
                } else {
                    mListener->onSctpDataChannelBinary(channel.label, msg.data);
                }
            }
            break;
        }
        return;
    }

    if (ppid != kPpidDcep || r.remaining() < 1)
        return;

    const auto msgType = r.readU8();

    if (msgType == kDcepMsgAck) {
        for (auto& channel : mDataChannels) {
            if (channel.streamId == streamId && channel.state == DataChannelState::kOpening) {
                Task::cancelHelper(channel.taskT1Open);
                channel.state = DataChannelState::kOpen;
                channel.receiveBuffer.consumeSsn(ssn);
                mListener->onSctpDataChannelOpen(channel.label);
                break;
            }
        }
    } else if (msgType == kDcepMsgOpen) {
        // channelType(1) + priority(2) + reliabilityParameter(4) + labelLength(2) + protocolLength(2)
        if (r.remaining() < 11)
            return;
        const auto channelType = r.readU8();
        const bool unordered = (channelType & 0x80) != 0;
        r.skip(6); // priority, reliabilityParameter
        const auto labelLen = r.readU16();
        r.skip(2); // protocolLength
        if (labelLen > r.remaining()) {
            return;
        }
        const auto label = r.readString(labelLen);

        auto& channel = mDataChannels.emplace_back(label, streamId, unordered);
        channel.state = DataChannelState::kOpen;
        channel.receiveBuffer.consumeSsn(ssn);

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

        mListener->onSctpSendPacket(packet);
        mListener->onSctpDataChannelOpen(label);
    }
}

void SctpSession::onReceiveReconfig(const SctpPacket::Chunk& chunk)
{
    // RECONFIG chunk body is a sequence of parameters starting at offset 0
    for (const auto& param : chunk.parseParams(0)) {
        if (param.type != kParamOutgoingSsnReset || param.size < 12)
            continue;

        // Outgoing SSN Reset Request layout (RFC 6525 §4.1):
        //   requestSeqNum(4) + responseSeqNum(4) + lastTsn(4) + streamIds(2 each)
        ByteReader r(param.data, param.size);
        const auto requestSeqNum = r.readU32();
        r.skip(8); // responseSeqNum, lastTsn

        const auto numStreams = r.remaining() / 2;
        std::vector<uint16_t> streamIds;
        streamIds.reserve(numStreams);
        for (size_t i = 0; i < numStreams; ++i) {
            streamIds.push_back(r.readU16());
        }

        // Close matching channels and notify listener
        for (const auto streamId : streamIds) {
            for (auto it = mDataChannels.begin(); it != mDataChannels.end(); ++it) {
                if (it->streamId == streamId) {
                    Task::cancelHelper(it->taskT1Open);
                    mListener->onSctpDataChannelClose(it->label);
                    mDataChannels.erase(it);
                    break;
                }
            }
        }

        // Send Re-Config Response acknowledging the request
        ByteBuffer responseParam;
        ByteWriter pw(responseParam);
        pw.writeU16(kParamReconfigResponse);
        pw.writeU16(12); // type(2) + length(2) + seqNum(4) + result(4)
        pw.writeU32(requestSeqNum);
        pw.writeU32(kReconfigResultSuccess);

        SctpPacketBuilder builder(mLocalPort, mRemotePort, mPeerTag);
        builder.addChunk(kChunkReconfig, 0, responseParam);
        auto packet = builder.build();
        mListener->onSctpSendPacket(packet);
    }
}

void SctpSession::onReceiveSack(const SctpPacket::Chunk& chunk)
{
    if (chunk.size < 12) {
        return;
    }

    ByteReader r(chunk.data, chunk.size);
    const auto cumTsn = r.readU32();
    const auto aRwnd = r.readU32();
    const auto numGapBlocks = r.readU16();
    r.skip(2); // numDupTsns

    std::vector<std::pair<uint16_t, uint16_t>> gapBlocks;
    gapBlocks.reserve(numGapBlocks);
    for (uint16_t i = 0; i < numGapBlocks; ++i) {
        const auto start = r.readU16();
        const auto end = r.readU16();
        gapBlocks.emplace_back(start, end);
    }

    mPeerRwnd = aRwnd;

    bool anyNewlyAcked = false;

    // Remove cumulatively ACKed chunks
    auto it = mSentChunks.begin();
    while (it != mSentChunks.end()) {
        if (static_cast<int32_t>(it->tsn - cumTsn) <= 0) {
            mFlightSize = mFlightSize >= it->payloadSize ? mFlightSize - it->payloadSize : 0;
            it = mSentChunks.erase(it);
            anyNewlyAcked = true;
        } else {
            ++it;
        }
    }

    // Remove gap-ACKed chunks
    for (const auto& [gapStart, gapEnd] : gapBlocks) {
        auto git = mSentChunks.begin();
        while (git != mSentChunks.end()) {
            const auto offset = static_cast<uint16_t>(git->tsn - cumTsn);
            if (offset >= gapStart && offset <= gapEnd) {
                mFlightSize = mFlightSize >= git->payloadSize ? mFlightSize - git->payloadSize : 0;
                git = mSentChunks.erase(git);
                anyNewlyAcked = true;
            } else {
                ++git;
            }
        }
    }

    if (mSentChunks.empty()) {
        stopT3Rtx();
    } else if (anyNewlyAcked) {
        startT3Rtx(); // restart on progress
    }

    if (anyNewlyAcked) {
        transmitPending();
    }
}

void SctpSession::retransmitOldest()
{
    if (mSentChunks.empty()) {
        return;
    }

    const auto& chunk = mSentChunks.front();
    SctpPacketBuilder builder(mLocalPort, mRemotePort, mPeerTag);
    builder.addChunk(kChunkData, chunk.flags, chunk.body);
    mListener->onSctpSendPacket(builder.build());

    startT3Rtx(); // restart for next potential timeout
}

void SctpSession::startT3Rtx()
{
    Task::cancelHelper(mTaskT3Rtx);
    mTaskT3Rtx = mScheduler.submit(retransmitDelay(0), __FILE__, __LINE__, [this] { retransmitOldest(); });
}

void SctpSession::stopT3Rtx()
{
    Task::cancelHelper(mTaskT3Rtx);
}

void SctpSession::sendSack()
{
    // Build gap ack blocks from out-of-order TSNs.
    // Offsets are relative to mPeerCumulativeTsn; sort by offset to handle
    // TSN wraparound correctly (uint32_t distance, not raw value order).
    struct GapBlock {
        uint16_t start, end;
    };
    std::vector<GapBlock> gapBlocks;

    if (!mPeerOutOfOrderTsns.empty()) {
        std::vector<uint32_t> tsns(mPeerOutOfOrderTsns.begin(), mPeerOutOfOrderTsns.end());
        std::sort(tsns.begin(), tsns.end(), [this](uint32_t a, uint32_t b) {
            return (uint32_t)(a - mPeerCumulativeTsn) < (uint32_t)(b - mPeerCumulativeTsn);
        });

        uint16_t blockStart = 0, blockEnd = 0;
        bool inBlock = false;
        for (const auto tsn : tsns) {
            const auto offset = static_cast<uint16_t>(tsn - mPeerCumulativeTsn);
            if (!inBlock) {
                blockStart = blockEnd = offset;
                inBlock = true;
            } else if (offset == static_cast<uint16_t>(blockEnd + 1)) {
                blockEnd = offset;
            } else {
                gapBlocks.push_back({ blockStart, blockEnd });
                blockStart = blockEnd = offset;
            }
        }
        if (inBlock) {
            gapBlocks.push_back({ blockStart, blockEnd });
        }
    }

    ByteBuffer body;
    ByteWriter w(body);
    w.writeU32(mPeerCumulativeTsn);
    w.writeU32(kInitRwnd);
    w.writeU16(static_cast<uint16_t>(gapBlocks.size()));
    w.writeU16(0); // duplicate TSNs not tracked
    for (const auto& gb : gapBlocks) {
        w.writeU16(gb.start);
        w.writeU16(gb.end);
    }

    SctpPacketBuilder builder(mLocalPort, mRemotePort, mPeerTag);
    builder.addChunk(kChunkSack, 0, body);
    auto packet = builder.build();
    mListener->onSctpSendPacket(packet);
}

} // namespace srtc::sctp

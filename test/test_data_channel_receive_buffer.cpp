#include <gtest/gtest.h>

#include "../src/sctp/data_channel_receive_buffer.h"
#include "../src/sctp/sctp_defs.h"

using namespace srtc::sctp;

// ── helpers ──────────────────────────────────────────────────────────────────

static std::vector<uint8_t> makePayload(char fill, size_t size)
{
    return std::vector<uint8_t>(size, static_cast<uint8_t>(fill));
}

static std::list<DataChannelReceiveBuffer::Message> recv(
    DataChannelReceiveBuffer& buf,
    uint16_t ssn,
    uint8_t flags,
    uint32_t ppid,
    const std::vector<uint8_t>& payload)
{
    return buf.receive(ssn, flags, /*unordered=*/false, ppid, payload.data(), payload.size());
}

static std::list<DataChannelReceiveBuffer::Message> recvUnordered(
    DataChannelReceiveBuffer& buf,
    uint16_t ssn,
    uint8_t flags,
    uint32_t ppid,
    const std::vector<uint8_t>& payload)
{
    return buf.receive(ssn, flags, /*unordered=*/true, ppid, payload.data(), payload.size());
}

// ── single complete message ───────────────────────────────────────────────────

TEST(TestSctpReceiveBuffer, SingleCompleteMessage)
{
    DataChannelReceiveBuffer buf;
    auto payload = makePayload('A', 100);
    auto out = recv(buf, 0, kDataFlagComplete, kPpidString, payload);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out.front().ppid, kPpidString);
    EXPECT_EQ(out.front().data.size(), 100u);
    EXPECT_EQ(out.front().data.data()[0], 'A');
}

// ── two-fragment message ──────────────────────────────────────────────────────

TEST(TestSctpReceiveBuffer, TwoFragments)
{
    DataChannelReceiveBuffer buf;
    auto first  = makePayload('X', 600);
    auto second = makePayload('Y', 400);

    // First fragment (B=1, E=0): no output yet
    auto out1 = recv(buf, 0, kDataFlagB, kPpidBinary, first);
    EXPECT_TRUE(out1.empty());

    // Last fragment (B=0, E=1): assembled message delivered
    auto out2 = recv(buf, 0, kDataFlagE, kPpidBinary, second);
    ASSERT_EQ(out2.size(), 1u);

    const auto& msg = out2.front();
    EXPECT_EQ(msg.ppid, kPpidBinary);
    ASSERT_EQ(msg.data.size(), 1000u);
    // First 600 bytes from first fragment, next 400 from second
    EXPECT_EQ(msg.data.data()[0],   'X');
    EXPECT_EQ(msg.data.data()[599], 'X');
    EXPECT_EQ(msg.data.data()[600], 'Y');
    EXPECT_EQ(msg.data.data()[999], 'Y');
}

// ── three-fragment message ────────────────────────────────────────────────────

TEST(TestSctpReceiveBuffer, ThreeFragments)
{
    DataChannelReceiveBuffer buf;

    auto out1 = recv(buf, 0, kDataFlagB, kPpidString, makePayload('A', 100));
    EXPECT_TRUE(out1.empty());

    auto out2 = recv(buf, 0, 0x00, kPpidString, makePayload('B', 100)); // middle
    EXPECT_TRUE(out2.empty());

    auto out3 = recv(buf, 0, kDataFlagE, kPpidString, makePayload('C', 100));
    ASSERT_EQ(out3.size(), 1u);
    EXPECT_EQ(out3.front().data.size(), 300u);
    EXPECT_EQ(out3.front().data.data()[0],   'A');
    EXPECT_EQ(out3.front().data.data()[100],  'B');
    EXPECT_EQ(out3.front().data.data()[200],  'C');
}

// ── in-order SSN delivery ─────────────────────────────────────────────────────

TEST(TestSctpReceiveBuffer, InOrderDelivery)
{
    DataChannelReceiveBuffer buf;

    auto out0 = recv(buf, 0, kDataFlagComplete, kPpidString, makePayload('0', 10));
    auto out1 = recv(buf, 1, kDataFlagComplete, kPpidString, makePayload('1', 10));
    auto out2 = recv(buf, 2, kDataFlagComplete, kPpidString, makePayload('2', 10));

    ASSERT_EQ(out0.size(), 1u); EXPECT_EQ(out0.front().data.data()[0], '0');
    ASSERT_EQ(out1.size(), 1u); EXPECT_EQ(out1.front().data.data()[0], '1');
    ASSERT_EQ(out2.size(), 1u); EXPECT_EQ(out2.front().data.data()[0], '2');
}

// ── out-of-order: gap fills unblock buffered messages ─────────────────────────

TEST(TestSctpReceiveBuffer, OutOfOrderUnblocks)
{
    DataChannelReceiveBuffer buf;

    // SSN 2 and 3 arrive before SSN 1
    auto out2 = recv(buf, 2, kDataFlagComplete, kPpidString, makePayload('2', 10));
    auto out3 = recv(buf, 3, kDataFlagComplete, kPpidString, makePayload('3', 10));
    EXPECT_TRUE(out2.empty());
    EXPECT_TRUE(out3.empty());

    // SSN 0 arrives, delivers 0 only
    auto out0 = recv(buf, 0, kDataFlagComplete, kPpidString, makePayload('0', 10));
    ASSERT_EQ(out0.size(), 1u);
    EXPECT_EQ(out0.front().data.data()[0], '0');

    // SSN 1 arrives, delivers 1 + 2 + 3
    auto out1 = recv(buf, 1, kDataFlagComplete, kPpidString, makePayload('1', 10));
    ASSERT_EQ(out1.size(), 3u);
    auto it = out1.begin();
    EXPECT_EQ(it->data.data()[0], '1'); ++it;
    EXPECT_EQ(it->data.data()[0], '2'); ++it;
    EXPECT_EQ(it->data.data()[0], '3');
}

// ── duplicate (already-delivered) SSN is discarded ───────────────────────────

TEST(TestSctpReceiveBuffer, DuplicateDiscarded)
{
    DataChannelReceiveBuffer buf;

    auto out0 = recv(buf, 0, kDataFlagComplete, kPpidString, makePayload('A', 10));
    ASSERT_EQ(out0.size(), 1u);
    EXPECT_EQ(out0.front().data.data()[0], 'A');

    // Retransmit of SSN 0 — must be silently dropped, 'A' not replaced by 'Z'
    auto dup = recv(buf, 0, kDataFlagComplete, kPpidString, makePayload('Z', 10));
    EXPECT_TRUE(dup.empty());

    // SSN 1 still delivers normally
    auto out1 = recv(buf, 1, kDataFlagComplete, kPpidString, makePayload('B', 10));
    ASSERT_EQ(out1.size(), 1u);
    EXPECT_EQ(out1.front().data.data()[0], 'B');
}

// ── consumeSsn advances past DCEP control messages ───────────────────────────

TEST(TestSctpReceiveBuffer, ConsumeSsnSkipsDcep)
{
    DataChannelReceiveBuffer buf;

    // SSN 0 consumed by DATA_CHANNEL_OPEN/ACK
    buf.consumeSsn(0);

    // First user message at SSN 1 must be delivered immediately
    auto out = recv(buf, 1, kDataFlagComplete, kPpidString, makePayload('X', 20));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out.front().data.size(), 20u);
}

// ── SSN wraparound ────────────────────────────────────────────────────────────

TEST(TestSctpReceiveBuffer, SsnWraparound)
{
    DataChannelReceiveBuffer buf;

    // Wind mNextSsn up to 65534
    for (uint16_t ssn = 0; ssn < 65534; ++ssn) {
        buf.consumeSsn(ssn);
    }

    // 65534, 65535, 0 (wrapped) all arrive in order
    auto a = recv(buf, 65534, kDataFlagComplete, kPpidString, makePayload('A', 5));
    auto b = recv(buf, 65535, kDataFlagComplete, kPpidString, makePayload('B', 5));
    auto c = recv(buf, 0,     kDataFlagComplete, kPpidString, makePayload('C', 5));

    ASSERT_EQ(a.size(), 1u); EXPECT_EQ(a.front().data.data()[0], 'A');
    ASSERT_EQ(b.size(), 1u); EXPECT_EQ(b.front().data.data()[0], 'B');
    ASSERT_EQ(c.size(), 1u); EXPECT_EQ(c.front().data.data()[0], 'C');
}

// ── unordered: delivered immediately regardless of SSN ───────────────────────

TEST(TestSctpReceiveBuffer, UnorderedDeliveredImmediately)
{
    DataChannelReceiveBuffer buf;

    // Unordered messages arrive out of SSN order
    auto out2 = recvUnordered(buf, 2, kDataFlagComplete, kPpidBinary, makePayload('2', 8));
    auto out0 = recvUnordered(buf, 0, kDataFlagComplete, kPpidBinary, makePayload('0', 8));
    auto out1 = recvUnordered(buf, 1, kDataFlagComplete, kPpidBinary, makePayload('1', 8));

    ASSERT_EQ(out2.size(), 1u); EXPECT_EQ(out2.front().data.data()[0], '2');
    ASSERT_EQ(out0.size(), 1u); EXPECT_EQ(out0.front().data.data()[0], '0');
    ASSERT_EQ(out1.size(), 1u); EXPECT_EQ(out1.front().data.data()[0], '1');
}

// ── unordered two-fragment message ───────────────────────────────────────────

TEST(TestSctpReceiveBuffer, UnorderedTwoFragments)
{
    DataChannelReceiveBuffer buf;

    auto out1 = recvUnordered(buf, 0, kDataFlagB, kPpidBinary, makePayload('X', 300));
    EXPECT_TRUE(out1.empty());

    auto out2 = recvUnordered(buf, 0, kDataFlagE, kPpidBinary, makePayload('Y', 300));
    ASSERT_EQ(out2.size(), 1u);
    EXPECT_EQ(out2.front().data.size(), 600u);
    EXPECT_EQ(out2.front().data.data()[0],   'X');
    EXPECT_EQ(out2.front().data.data()[300],  'Y');
}

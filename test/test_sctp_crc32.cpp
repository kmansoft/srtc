#include <gtest/gtest.h>

#include "../src/sctp/sctp_crc32.h"

#include <cstring>

using namespace srtc::sctp;

// Trivially-correct bit-by-bit CRC-32c using the reflected Castagnoli polynomial.
// Used to generate expected values without hardcoding anything we can't verify offline.
static uint32_t reference_crc32c(const uint8_t* data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1) crc = (crc >> 1) ^ 0x82F63B78u;
            else         crc >>= 1;
        }
    }
    return ~crc;
}

// Canonical check value — universally agreed upon for CRC-32c.

TEST(TestSctpCrc32, Check123456789)
{
    const uint8_t buf[] = {'1','2','3','4','5','6','7','8','9'};
    EXPECT_EQ(reference_crc32c(buf, sizeof(buf)), 0xE3069283u);
    EXPECT_EQ(crc32c(buf, sizeof(buf)), 0xE3069283u);
}

// RFC 3720 Appendix B.4 test vectors, as used by the Google CRC-32c library
// (https://github.com/google/crc32c) which cites this RFC as its validation source.

TEST(TestSctpCrc32, Rfc3720Zeros)
{
    uint8_t buf[32] = {};
    EXPECT_EQ(crc32c(buf, 32), 0x8A9136AAu);
}

TEST(TestSctpCrc32, Rfc3720Ffs)
{
    uint8_t buf[32];
    memset(buf, 0xFF, 32);
    EXPECT_EQ(crc32c(buf, 32), 0x62A8AB43u);
}

TEST(TestSctpCrc32, Rfc3720Sequential)
{
    uint8_t buf[32];
    for (int i = 0; i < 32; i++) buf[i] = static_cast<uint8_t>(i);
    EXPECT_EQ(crc32c(buf, 32), 0x46DD794Eu);
}

// Compare fast implementation against the bit-by-bit reference for lengths 0..64.
// Covers the singletable path (< 4 bytes) and the slicing-by-8 loop.

TEST(TestSctpCrc32, FastVsReferenceAllLengths)
{
    uint8_t buf[64];
    for (int i = 0; i < 64; i++) {
        buf[i] = static_cast<uint8_t>(i * 3 + 7);
    }

    for (size_t len = 0; len <= 64; len++) {
        const uint32_t expected = reference_crc32c(buf, len);
        const uint32_t actual   = crc32c(buf, len);
        EXPECT_EQ(actual, expected) << "length " << len;
    }
}

// Verify incremental API matches single-call for all split points.

TEST(TestSctpCrc32, IncrementalSplitConsistency)
{
    uint8_t buf[64];
    for (int i = 0; i < 64; i++) {
        buf[i] = static_cast<uint8_t>(i * 3 + 7);
    }

    const uint32_t expected = crc32c(buf, sizeof(buf));

    for (size_t split = 0; split <= sizeof(buf); split++) {
        uint32_t crc = crc32c_update(0xFFFFFFFFu, buf, split);
        crc = crc32c_update(crc, buf + split, sizeof(buf) - split);
        const uint32_t actual = crc32c_finalize(crc);
        EXPECT_EQ(actual, expected) << "split at " << split;
    }
}

// Byte-at-a-time incremental matches single-call for lengths 0..16.

TEST(TestSctpCrc32, ByteAtATimeConsistency)
{
    uint8_t buf[16];
    for (int i = 0; i < 16; i++) {
        buf[i] = static_cast<uint8_t>(i + 1);
    }

    for (size_t len = 0; len <= 16; len++) {
        const uint32_t expected = crc32c(buf, len);

        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; i++) {
            crc = crc32c_update(crc, buf + i, 1);
        }
        const uint32_t actual = crc32c_finalize(crc);
        EXPECT_EQ(actual, expected) << "length " << len;
    }
}

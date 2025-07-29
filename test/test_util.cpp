#include <gtest/gtest.h>

#include "srtc/util.h"

#include <iostream>

// NACK compression

TEST(TestUtil, TestCompressNackList)
{
    size_t n;
    uint16_t seq_list[16];
    uint16_t blp_list[16];

    // Empty list
    n = srtc::compressNackList({}, seq_list, blp_list);
    ASSERT_EQ(n, 0);

    // One value
    n = srtc::compressNackList({ 1 }, seq_list, blp_list);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(seq_list[0], 1);
    ASSERT_EQ(blp_list[0], 0);

    // Two adjacent values
    n = srtc::compressNackList({ 1, 2, 3 }, seq_list, blp_list);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(seq_list[0], 1);
    ASSERT_EQ(blp_list[0], 1 | (1 << 1));

    // More values
    n = srtc::compressNackList({ 1, 2, 17 }, seq_list, blp_list);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(seq_list[0], 1);
    ASSERT_EQ(blp_list[0], 1 | (1 << 15));

    // Even more values
    n = srtc::compressNackList({ 1, 3, 18, 19, 20 }, seq_list, blp_list);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(seq_list[0], 1);
    ASSERT_EQ(blp_list[0], 1 << 1);
    ASSERT_EQ(seq_list[1], 18);
    ASSERT_EQ(blp_list[1], 1 | (1 << 1));

    // Rollover
    n = srtc::compressNackList({ 65530, 1, 20, 21 }, seq_list, blp_list);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(seq_list[0], 65530);
    ASSERT_EQ(blp_list[0], 1 << 6);
    ASSERT_EQ(seq_list[1], 20);
    ASSERT_EQ(blp_list[1], 1);
}

#include <gtest/gtest.h>

#include "srtc/byte_buffer.h"
#include "srtc/util.h"

#include <iostream>

// LEB128

TEST(GoogleVLA, LEB128)
{
    std::cout << "LEB128" << std::endl;

    const std::pair<uint32_t, const char*> gTestData[] = {
            {0,    "00"},
            {1,    "01"},
            {127,  "7f"},
            {128,  "80:01"},
            {500,  "f4:03"},
            {1500, "dc:0b"},
            {2500, "c4:13"}
    };

    for (const auto& pair : gTestData) {
        srtc::ByteBuffer buf;
        srtc::ByteWriter w(buf);

        w.writeLEB128(pair.first);
        const auto actual = srtc::bin_to_hex(buf.data(), buf.size());
        const auto expected = pair.second;

        ASSERT_EQ(expected, actual);
    }
}

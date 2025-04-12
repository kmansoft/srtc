#include <gtest/gtest.h>

#include "srtc/byte_buffer.h"
#include "srtc/util.h"
#include "srtc/simulcast_layer.h"
#include "srtc/rtp_extension_builder.h"

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

TEST(GoogleVLA, VLA)
{
    std::cout << "VLA" << std::endl;

    const std::vector<srtc::SimulcastLayer> kLayerList = {
            {
                "low",
                320, 180,
                15,
                500
            },
            {
                    "mid",
                    640, 360,
                    15,
                    1500
            },
            {
                    "high",
                    1280, 720,
                    15,
                    2500
            }
    };

    const std::string kLayerEncoded[] = {
            "21:00:f4:03:dc:0b:c4:13:01:3f:00:b3:0f:02:7f:01:67:0f:04:ff:02:cf:0f",
            "61:00:f4:03:dc:0b:c4:13:01:3f:00:b3:0f:02:7f:01:67:0f:04:ff:02:cf:0f",
            "a1:00:f4:03:dc:0b:c4:13:01:3f:00:b3:0f:02:7f:01:67:0f:04:ff:02:cf:0f"
    };

    for (auto i = 0; i < 3; i += 1) {
        srtc::RtpExtensionBuilder builder;
        builder.addGoogleVLA(100, i, kLayerList);

        const auto extension = builder.build();

        ASSERT_FALSE(extension.empty());

        const auto& data = extension.getData();
        const auto encoded = srtc::bin_to_hex(data.data() + 2, data.size() - 2);
        const auto expected = kLayerEncoded[i];

        ASSERT_EQ(encoded, expected);
    }
}

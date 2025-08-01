#include <gtest/gtest.h>

#include "srtc/byte_buffer.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/simulcast_layer.h"
#include "srtc/util.h"

#include <iostream>

// LEB128

TEST(GoogleVLA, LEB128)
{
    const std::pair<uint32_t, const char*> gTestData[] = { { 0, "00" },      { 1, "01" },      { 127, "7f" },
                                                           { 128, "80:01" }, { 500, "f4:03" }, { 1500, "dc:0b" },
                                                           { 2500, "c4:13" } };

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
    const std::vector<srtc::SimulcastLayer> kLayerList = { { "low", 320, 180, 15, 500 },
                                                           { "mid", 640, 360, 15, 1500 },
                                                           { "high", 1280, 720, 15, 2500 } };

    const std::string kLayerEncoded[] = { "21:00:f4:03:dc:0b:c4:13:01:3f:00:b3:0f:02:7f:01:67:0f:04:ff:02:cf:0f",
                                          "61:00:f4:03:dc:0b:c4:13:01:3f:00:b3:0f:02:7f:01:67:0f:04:ff:02:cf:0f",
                                          "a1:00:f4:03:dc:0b:c4:13:01:3f:00:b3:0f:02:7f:01:67:0f:04:ff:02:cf:0f" };

    std::vector<std::shared_ptr<srtc::SimulcastLayer>> layerList;
    for (const auto& layer : kLayerList) {
        layerList.push_back(std::make_shared<srtc::SimulcastLayer>(layer));
    }

    for (auto i = 0; i < 3; i += 1) {
        srtc::ByteBuffer data;
        srtc::buildGoogleVLA(data, i, layerList);
        ASSERT_FALSE(data.empty());

        const auto encoded = srtc::bin_to_hex(data.data(), data.size());
        const auto expected = kLayerEncoded[i];

        ASSERT_EQ(encoded, expected);
    }
}

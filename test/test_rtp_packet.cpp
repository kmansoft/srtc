#include <gtest/gtest.h>

#include "srtc/byte_buffer.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_packet.h"
#include "srtc/media.h"
#include "srtc/track.h"
#include "srtc/util.h"

#include <cstring>
#include <iostream>

#include <openssl/rand.h>

namespace
{

uint32_t randomU32()
{
    uint32_t value;
    RAND_bytes((unsigned char*)&value, sizeof(value));
    return value;
}

} // namespace

// Parsing a one-byte format extension

TEST(Extension, OneByteFormat)
{
    srtc::ByteBuffer one;
    srtc::ByteWriter one_w(one);

    // U16
    one_w.writeU8((1 << 4) | 1);
    one_w.writeU16(0x1234);

    // U32
    one_w.writeU8((2 << 4) | 3);
    one_w.writeU32(0x12345678);

    // String
    const std::string s("testing");
    one_w.writeU8((3 << 4) | (s.length() - 1));
    one_w.write(reinterpret_cast<const uint8_t*>(s.data()), s.size());

    const srtc::RtpExtension extension(srtc::RtpExtension::kOneByte, std::move(one));

    const auto u16 = extension.findU16(1);
    ASSERT_TRUE(u16.has_value());
    ASSERT_EQ(0x1234, u16.value());

    const auto u32 = extension.findU32(2);
    ASSERT_TRUE(u32.has_value());
    ASSERT_EQ(0x12345678u, u32.value());

    const auto str = extension.findAny(3);
    ASSERT_TRUE(str.has_value());
    ASSERT_EQ(s.size(), str->size);
    ASSERT_EQ(0, std::memcmp(s.data(), str->ptr, s.size()));

    // Not present
    ASSERT_FALSE(extension.findU16(4).has_value());
}

// Parsing a two-byte format extension

TEST(Extension, TwoByteFormat)
{
    srtc::ByteBuffer two;
    srtc::ByteWriter two_w(two);

    // U16
    two_w.writeU8(1);
    two_w.writeU8(2);
    two_w.writeU16(0x1234);

    // U32
    two_w.writeU8(2);
    two_w.writeU8(4);
    two_w.writeU32(0x12345678);

    // U64
    two_w.writeU8(3);
    two_w.writeU8(8);
    two_w.writeU64(0x123456789ABCDEF0ull);

    // String
    const std::string s("testing");
    two_w.writeU8(4);
    two_w.writeU8(static_cast<uint8_t>(s.size()));
    two_w.write(reinterpret_cast<const uint8_t*>(s.data()), s.size());

    const srtc::RtpExtension extension(srtc::RtpExtension::kTwoByte, std::move(two));

    const auto u16 = extension.findU16(1);
    ASSERT_TRUE(u16.has_value());
    ASSERT_EQ(0x1234, u16.value());

    const auto u32 = extension.findU32(2);
    ASSERT_TRUE(u32.has_value());
    ASSERT_EQ(0x12345678u, u32.value());

    const auto u64 = extension.findU64(3);
    ASSERT_TRUE(u64.has_value());
    ASSERT_EQ(0x123456789ABCDEF0ull, u64.value());

    const auto str = extension.findAny(4);
    ASSERT_TRUE(str.has_value());
    ASSERT_EQ(s.size(), str->size);
    ASSERT_EQ(0, std::memcmp(s.data(), str->ptr, s.size()));

    // Not present
    ASSERT_FALSE(extension.findU16(5).has_value());
}

// RTP packet to and from UDP

TEST(RtpPacket, Serialize)
{
    const auto kSSRC = 0x12345678u;
    const auto kPayloadId = 96u;

    const auto media = std::make_shared<srtc::Media>("0", srtc::MediaType::Video);
    const auto track = std::make_shared<srtc::Track>(media,
                                                     srtc::Direction::Subscribe,
                                                     kSSRC,
                                                     kPayloadId,
                                                     0,
                                                     0,
                                                     srtc::Codec::H264,
                                                     nullptr,
                                                     nullptr,
                                                     90000,
                                                     false,
                                                     false);

    for (size_t i = 0; i < 5000; i += 1) {
        uint8_t padding = 0;
        if ((i % 5) == 0) {
            padding = randomU32() & 0xFF;
        }

        srtc::RtpExtension extension;
        if ((i % 7) == 0) {
            srtc::RtpExtensionBuilder builder;

            builder.addStringValue(1, "foo");
            builder.addStringValue(2, "bar");
            builder.addU16Value(3, 0x1111);
            builder.addU16Value(4, 0x2222);

            extension = builder.build();
        }

        bool marker = false;
        if ((i % 9) == 0) {
            marker = true;
        }

        size_t payloadSize = randomU32() % 0x3FF;
        srtc::ByteBuffer payload(payloadSize);
        payload.resize(payloadSize);
        RAND_bytes(payload.data(), static_cast<int>(payloadSize));

        // This is our packet's unencrypted data
        const auto packet = std::make_shared<srtc::RtpPacket>(
            track, marker, 0, static_cast<uint16_t>(i), static_cast<uint32_t>(i), padding, extension.copy(), std::move(payload));

        // Generate
        const auto data = packet->generate();

        // Restore
        const auto copy = srtc::RtpPacket::fromUdpPacket(track, data.buf);

        ASSERT_TRUE(copy) << " iteration = " << i << std::endl;

        ASSERT_EQ(packet->getSSRC(), copy->getSSRC());
        ASSERT_EQ(packet->getPayloadId(), copy->getPayloadId());
        ASSERT_EQ(packet->getMarker(), copy->getMarker());
        ASSERT_EQ(packet->getPayloadSize(), copy->getPayloadSize());

        const auto& payload_source = packet->getPayload();
        const auto& payload_copy = copy->getPayload();

        ASSERT_EQ(payloadSize, payload_source.size());
        ASSERT_EQ(payloadSize, payload_copy.size());

        for (size_t s = 0; s < payloadSize; s += 1) {
            ASSERT_EQ(payload_source.data()[s], payload_copy.data()[s]);
        }

        const auto& extension_copy = copy->getExtension();
        ASSERT_EQ(extension.getId(), extension_copy.getId());

        const auto& extension_data = extension.getData();
        const auto& extension_copy_data = extension_copy.getData();

        ASSERT_EQ(extension_data.size(), extension_copy_data.size());
        for (size_t q = 0; q < extension_data.size(); q += 1) {
            ASSERT_EQ(extension_data.data()[q], extension_copy_data.data()[q]);
        }
    }
}

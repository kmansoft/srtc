#include <gtest/gtest.h>

#include "srtc/byte_buffer.h"
#include "srtc/rtp_extension_builder.h"
#include "srtc/rtp_packet.h"
#include "srtc/track.h"
#include "srtc/util.h"

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

// Extension conversion from one byte to two byte format

TEST(Extension, Convert)
{
	std::cout << "Extension Convert" << std::endl;

	srtc::ByteBuffer one;
	srtc::ByteWriter one_w(one);

	// U16
	one_w.writeU8((1 << 4) | 1);
	one_w.writeU16(0x1111);

	// U32
	one_w.writeU8((2 << 4) | 3);
	one_w.writeU32(0x2222);

	// String
	const std::string s("testing");
	one_w.writeU8((3 << 4) | (s.length() - 1));
	one_w.write(reinterpret_cast<const uint8_t*>(s.data()), s.size());

	// Convert
	const auto two = srtc::RtpExtension::convertOneToTwoByte(one);
	srtc::ByteReader two_r(two);

	// Validate length
	ASSERT_EQ(one.size() + 3, two.size());

	// U16
	ASSERT_EQ(1, two_r.readU8());
	ASSERT_EQ(2, two_r.readU8());
	ASSERT_EQ(0x1111, two_r.readU16());

	// U32
	ASSERT_EQ(2, two_r.readU8());
	ASSERT_EQ(4, two_r.readU8());
	ASSERT_EQ(0x2222, two_r.readU32());

	// String
	ASSERT_EQ(3, two_r.readU8());
	ASSERT_EQ(s.size(), two_r.readU8());

	uint8_t q[16];
	two_r.read(q, s.size());
	ASSERT_EQ(reinterpret_cast<const char*>(q), s);
}

// RTP packet to and from UDP

TEST(RtpPacket, Serialize)
{
	const auto kSSRC = 0x12345678u;
	const auto kPayloadId = 96u;

	const auto track = std::make_shared<srtc::Track>(1,
													 srtc::Direction::Subscribe,
													 srtc::MediaType::Video,
													 "0",
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
		RAND_bytes(payload.data(), payloadSize);

		// This is our packet's unencrypted data
		const auto packet = std::make_shared<srtc::RtpPacket>(track, marker, 0, i, i, padding, extension.copy(), std::move(payload));

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

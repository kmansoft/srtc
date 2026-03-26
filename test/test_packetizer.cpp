#include <gtest/gtest.h>

#include "srtc/byte_buffer.h"
#include "srtc/codec_h264.h"
#include "srtc/depacketizer_h264.h"
#include "srtc/extended_value.h"
#include "srtc/packetizer_h264.h"
#include "srtc/rtp_extension_source_simulcast.h"
#include "srtc/rtp_extension_source_twcc.h"
#include "srtc/track.h"

#include <cstring>
#include <openssl/rand.h>

namespace
{

uint32_t randomU32()
{
    uint32_t value;
    RAND_bytes((unsigned char*)&value, sizeof(value));
    return value;
}

srtc::ByteBuffer generateNAL(uint8_t type, uint32_t size)
{
    srtc::ByteBuffer buffer;
    buffer.append(&type, 1);

    buffer.reserve(size + 1);
    buffer.resize(size + 1);
    RAND_bytes(buffer.data() + 1, static_cast<int>(size));

    return buffer;
}

void appendNAL(srtc::ByteBuffer& buffer, uint8_t type, uint32_t size)
{
    const auto nal = generateNAL(type, size);

    static constexpr uint8_t kAnnexB[] = { 0, 0, 0, 1 };
    buffer.append(kAnnexB, sizeof(kAnnexB));
    buffer.append(nal.data(), nal.size());
}

} // namespace

TEST(Packetizer, h264)
{
    std::cout << "H264 packetizer" << std::endl;

    const std::vector<srtc::SimulcastLayer> layerList = { { "high", 1280, 720, 30, 2500 },
                                                          { "medium", 640, 360, 30, 1000 },
                                                          { "low", 320, 180, 15, 500 } };
    const srtc::Track::SimulcastLayer layer0 = { layerList[0], 0 };

    const auto codecOptions = std::make_shared<srtc::Track::CodecOptions>(0x42e01fu, 0, false);

    const auto trackPublish =
        srtc::TrackBuilder(0, srtc::Direction::Publish, srtc::MediaType::Video, "video_0", 1234u, 96u, 90000u)
            .codec(srtc::Codec::H264, codecOptions)
            .simulcastLayer(std::make_shared<srtc::Track::SimulcastLayer>(layer0))
            .build();
    const auto trackSubscribe =
        srtc::TrackBuilder(0, srtc::Direction::Subscribe, srtc::MediaType::Video, "video_0", 5678u, 96u, 90000u)
            .codec(srtc::Codec::H264, codecOptions)
            .build();

    const auto packetizer = std::make_shared<srtc::PacketizerH264>(trackPublish);
    const auto depacketizer = std::make_shared<srtc::DepacketizerH264>(trackSubscribe);

    int64_t pts_usec = 1000u;

    const auto extensionSimulcast = std::make_shared<srtc::RtpExtensionSourceSimulcast>(1, 2, 3, 4);

    const auto scheduler = std::make_shared<srtc::ThreadScheduler>("test");
    const auto extensionTWCC = std::make_shared<srtc::RtpExtensionSourceTWCC>(5, 6, scheduler);

    srtc::ExtendedValue<uint16_t> extendedSeq;
    srtc::ExtendedValue<uint32_t> extendedRtpTime;

    for (size_t i = 0u; i < 1000; i += 1, pts_usec += 40u * 1000u) {
        // Generate random input frame
        srtc::ByteBuffer sourceFrame;

        if (i == 0u || (randomU32() % 100) < 10) {
            // Key frame
            extensionSimulcast->prepare(trackPublish, layerList);

            appendNAL(sourceFrame, srtc::h264::NaluType::SPS, 10u + randomU32() % 16);
            appendNAL(sourceFrame, srtc::h264::NaluType::PPS, 10u + randomU32() % 16);
            appendNAL(sourceFrame, srtc::h264::NaluType::KeyFrame, 2000u + randomU32() % 1000u);
        } else {
            // Non key frame
            extensionSimulcast->clear();
            appendNAL(sourceFrame, srtc::h264::NaluType::NonKeyFrame, 1000u + randomU32() % 1000u);
        }

        // Packetize
        const auto packetList = packetizer->generate(extensionSimulcast, extensionTWCC, 12u, pts_usec, sourceFrame);

        // Convert to jitter buffer entries
        std::vector<const srtc::JitterBufferItem*> jitterBufferItemList;
        for (size_t packetIndex = 0u; packetIndex < packetList.size(); packetIndex += 1) {
            const auto& packet = packetList[packetIndex];
            auto item = new srtc::JitterBufferItem();

            item->payload = packet->getPayload().copy();
            item->seq_ext = extendedSeq.extend(packet->getSequence());
            item->rtp_timestamp_ext = extendedRtpTime.extend(packet->getTimestamp());
            item->marker = packetIndex == packetList.size() - 1u;

            jitterBufferItemList.push_back(item);
        }

        // Depacketize
        std::vector<srtc::ByteBuffer> extractedFrameList;
        depacketizer->extract(extractedFrameList, jitterBufferItemList);

        // Verify
        ASSERT_EQ(extractedFrameList.size(), 1u);

        const auto& extractedFrame = extractedFrameList[0];
        ASSERT_EQ(extractedFrame.size(), sourceFrame.size());
        ASSERT_EQ(0u, std::memcmp(extractedFrame.data(), sourceFrame.data(), sourceFrame.size()));

        // Clean up
        for (const auto item : jitterBufferItemList) {
            delete item;
        }
    }
}

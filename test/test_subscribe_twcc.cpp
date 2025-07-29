#include <gtest/gtest.h>

#include "srtc/logging.h"
#include "srtc/twcc_subscribe.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#define LOG(level, ...) srtc::log(level, "TWCC", __VA_ARGS__)

namespace
{

class PacketMap
{
public:
    PacketMap();
    ~PacketMap();

    void setAsReceived(uint16_t seq, int64_t received_time_micros);
    void setAsNotReceived(uint16_t seq);

    [[nodiscard]] bool isReceived(uint16_t seq, int64_t received_time_micros) const;
    [[nodiscard]] bool isNotReceived(uint16_t seq) const;

private:
    struct PacketInfo {
        uint16_t seq;
        bool received_time_present;
        int64_t received_time_micros;
    };

    std::unordered_map<uint16_t, PacketInfo*> mImpl;

    void setImpl(PacketInfo* info);
};

PacketMap::PacketMap() = default;

PacketMap::~PacketMap()
{
    for (auto& iter : mImpl) {
        delete iter.second;
    }
}

void PacketMap::setAsReceived(uint16_t seq, int64_t received_time_micros)
{
    const auto info = new PacketInfo;

    info->seq = seq;
    info->received_time_present = true;
    info->received_time_micros = received_time_micros;

    setImpl(info);
}

void PacketMap::setAsNotReceived(uint16_t seq)
{
    const auto info = new PacketInfo;

    info->seq = seq;
    info->received_time_present = false;
    info->received_time_micros = 0;

    setImpl(info);
}

void PacketMap::setImpl(PacketInfo* info)
{
    assert(mImpl.find(info->seq) == mImpl.end());
    mImpl.insert({ info->seq, info });
}

bool PacketMap::isReceived(uint16_t seq, int64_t received_time_micros) const
{
    const auto iter = mImpl.find(seq);
    if (iter == mImpl.end()) {
        return false;
    }

    const auto info = iter->second;
    return info->received_time_present && info->received_time_micros == received_time_micros;
}

bool PacketMap::isNotReceived(uint16_t seq) const
{
    const auto iter = mImpl.find(seq);
    if (iter == mImpl.end()) {
        return false;
    }

    const auto info = iter->second;
    return !info->received_time_present;
}

struct TempPacket {
    int32_t delta_micros;
    uint8_t status;
    bool received_time_present;
    int64_t received_time_micros;
};

bool isReceivedWithTime(uint8_t status)
{
    return status == srtc::twcc::kSTATUS_RECEIVED_SMALL_DELTA || status == srtc::twcc::kSTATUS_RECEIVED_LARGE_DELTA;
}

void processReport(PacketMap& packetMap, const srtc::ByteBuffer& buf)
{
    srtc::ByteReader reader(buf);

    // Read the header
    const uint16_t base_seq_number = reader.readU16();
    const uint16_t packet_status_count = reader.readU16();
    const uint32_t reference_time_and_fb_pkt_count = reader.readU32();

    const auto reference_time = static_cast<int32_t>(reference_time_and_fb_pkt_count >> 8);
    const auto fb_pkt_count = static_cast<uint8_t>(reference_time_and_fb_pkt_count & 0xFFu);
    (void)fb_pkt_count;

    const auto reference_time_micros = 64 * 1000 * static_cast<int64_t>(reference_time);

    // We'll need a packet buffer
    const auto tempList = std::make_unique<TempPacket[]>(packet_status_count);
    std::memset(tempList.get(), 0, sizeof(TempPacket) * packet_status_count);

    // Read the statuses
    // Be careful, this can wrap (and that's OK)
    const auto past_end_seq_number = static_cast<uint16_t>(base_seq_number + packet_status_count);

    // Read the chunks
    for (uint16_t seq_number = base_seq_number; seq_number != past_end_seq_number; /* do not increment */) {
        if (reader.remaining() < 2) {
            LOG(SRTC_LOG_E, "RTCP TWCC packet too small while reading chunk header");
            return;
        }

        const auto chunkHeader = reader.readU16();
        const auto chunkType = (chunkHeader >> 15) & 0x01;

        if (chunkType == srtc::twcc::kCHUNK_RUN_LENGTH) {
            // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.3
            const auto symbol = (chunkHeader >> 13) & 0x03u;
            const auto runLength = chunkHeader & 0x1FFFu;
            const uint16_t remaining = past_end_seq_number - seq_number;
            if (remaining < runLength || remaining > 0xFFFF) {
                LOG(SRTC_LOG_E, "RTCP TWCC packet: run_length %u is too large, remaining %u", runLength, remaining);
                break;
            }

            for (unsigned int j = 0; j < runLength; ++j) {
                const auto index = (seq_number + 0x10000 - base_seq_number) & 0xffff;
                assert(index >= 0);
                assert(index < packet_status_count);
                tempList[index].status = symbol;

                seq_number += 1;
                if (seq_number == past_end_seq_number) {
                    break;
                }
            }
        } else if (chunkType == srtc::twcc::kCHUNK_STATUS_VECTOR && ((chunkHeader >> 14) & 0x01) == 0) {
            // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
            for (uint16_t shift = 14; shift != 0; shift -= 1) {
                const auto symbol = ((chunkHeader >> (shift - 1)) & 0x01) ? srtc::twcc::kSTATUS_RECEIVED_SMALL_DELTA
                                                                          : srtc::twcc::kSTATUS_NOT_RECEIVED;

                const auto index = (seq_number + 0x10000 - base_seq_number) & 0xffff;
                assert(index >= 0);
                assert(index < packet_status_count);
                tempList[index].status = symbol;

                seq_number += 1;
                if (seq_number == past_end_seq_number) {
                    break;
                }
            }
        } else if (chunkType == srtc::twcc::kCHUNK_STATUS_VECTOR && ((chunkHeader >> 14) & 0x01) == 1) {
            // https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01#section-3.1.4
            for (uint16_t shift = 14; shift != 0; shift -= 2) {
                const auto symbol = (chunkHeader >> (shift - 2)) & 0x03;

                const auto index = (seq_number + 0x10000 - base_seq_number) & 0xffff;
                assert(index >= 0);
                assert(index < packet_status_count);
                tempList[index].status = symbol;

                seq_number += 1;
                if (seq_number == past_end_seq_number) {
                    break;
                }
            }
        } else {
            LOG(SRTC_LOG_E, "RTCP TWCC packet: unknown chunk type %u", chunkType);
            return;
        }
    }

    // Read the time deltas
    for (uint16_t i = 0; i < packet_status_count; ++i) {
        const auto symbol = tempList[i].status;

        if (symbol == srtc::twcc::kSTATUS_RECEIVED_SMALL_DELTA) {
            if (reader.remaining() < 1) {
                LOG(SRTC_LOG_E, "RTCP TWCC packet too small while reading small delta");
                return;
            }

            const auto delta = reader.readU8();
            tempList[i].delta_micros = 250 * delta;
        } else if (symbol == srtc::twcc::kSTATUS_RECEIVED_LARGE_DELTA) {
            if (reader.remaining() < 2) {
                LOG(SRTC_LOG_E, "RTCP TWCC packet too small while reading large delta");
                return;
            }

            const auto delta = static_cast<int16_t>(reader.readU16());
            tempList[i].delta_micros = 250 * delta;
        }
    }

    // We should have no data left
    if (reader.remaining() > 0) {
        LOG(SRTC_LOG_E, "There is data remaining after reading TWCC feedback packet");
        return;
    }

    // Resolve time deltas to absolute times
    TempPacket* prev_ptr = nullptr;

    for (uint16_t i = 0; i < packet_status_count; i += 1) {
        const auto curr_ptr = tempList.get() + i;

        if (isReceivedWithTime(tempList[i].status)) {
            if (!prev_ptr) {
                curr_ptr->received_time_micros = reference_time_micros + tempList[i].delta_micros;
            } else {
                curr_ptr->received_time_micros = prev_ptr->received_time_micros + tempList[i].delta_micros;
            }

            curr_ptr->received_time_present = true;
            prev_ptr = curr_ptr;
        }
    }

    // Store in the packet map
    for (uint16_t i = 0; i < packet_status_count; i += 1) {
        const auto curr_seq = static_cast<uint16_t>(base_seq_number + i);
        const auto curr_ptr = tempList.get() + i;

        if (curr_ptr->received_time_present) {
            packetMap.setAsReceived(curr_seq, curr_ptr->received_time_micros);
        } else {
            packetMap.setAsNotReceived(curr_seq);
        }
    }
}

void processReport(PacketMap& packetMap,
                   const std::shared_ptr<srtc::twcc::SubscribePacketHistory>& history,
                   int64_t now_micros)
{
    const auto list = history->generate(now_micros);
    for (const auto& buf : list) {
        processReport(packetMap, buf);
    }
}

} // namespace

TEST(TWCCResponder, SimpleSmall)
{
    constexpr auto kStep = 250; // microseconds

    const auto history = std::make_shared<srtc::twcc::SubscribePacketHistory>(1000000);

    history->saveIncomingPacket(20001, 3064000 + 1 * kStep);
    history->saveIncomingPacket(20002, 3064000 + 2 * kStep);
    history->saveIncomingPacket(20003, 3064000 + 3 * kStep);
    // 20004 is not received
    history->saveIncomingPacket(20005, 3064000 + 5 * kStep);
    history->saveIncomingPacket(20006, 3064000 + 6 * kStep);
    history->saveIncomingPacket(20007, 3064000 + 10 * kStep);
    history->saveIncomingPacket(20008, 3064000 + 11 * kStep);
    // 20009 is not received
    // 20010 is not received
    history->saveIncomingPacket(20011, 3064000 + 13 * kStep);

    PacketMap packet_map;
    processReport(packet_map, history, 0);

    ASSERT_TRUE(packet_map.isReceived(20001, 2064000 + 1 * kStep));
    ASSERT_TRUE(packet_map.isReceived(20002, 2064000 + 2 * kStep));
    ASSERT_TRUE(packet_map.isReceived(20003, 2064000 + 3 * kStep));
    ASSERT_TRUE(packet_map.isNotReceived(20004));
    ASSERT_TRUE(packet_map.isReceived(20005, 2064000 + 5 * kStep));
    ASSERT_TRUE(packet_map.isReceived(20006, 2064000 + 6 * kStep));
    ASSERT_TRUE(packet_map.isReceived(20007, 2064000 + 10 * kStep));
    ASSERT_TRUE(packet_map.isReceived(20008, 2064000 + 11 * kStep));
    ASSERT_TRUE(packet_map.isNotReceived(20009));
    ASSERT_TRUE(packet_map.isNotReceived(20010));
    ASSERT_TRUE(packet_map.isReceived(20011, 2064000 + 13 * kStep));
}

TEST(TWCCResponder, SimpleLarge)
{
    constexpr auto kStep = 300 * 250; // microseconds, too large to fit in a one byte delta

    const auto history = std::make_shared<srtc::twcc::SubscribePacketHistory>(1000000);

    history->saveIncomingPacket(20001, 3064000 + 1 * kStep);
    history->saveIncomingPacket(20002, 3064000 + 2 * kStep);
    history->saveIncomingPacket(20003, 3064000 + 3 * kStep);
    // 20004 is not received
    history->saveIncomingPacket(20005, 3064000 + 5 * kStep);
    history->saveIncomingPacket(20006, 3064000 + 6 * kStep);
    history->saveIncomingPacket(20007, 3064000 + 10 * kStep);
    history->saveIncomingPacket(20008, 3064000 + 11 * kStep);
    // 20009 is not received
    // 20010 is not received
    history->saveIncomingPacket(20011, 3064000 + 13 * kStep);

    PacketMap packet_map;
    processReport(packet_map, history, 0);

    ASSERT_TRUE(packet_map.isReceived(20001, 2064000 + 1 * kStep));
    ASSERT_TRUE(packet_map.isReceived(20002, 2064000 + 2 * kStep));
    ASSERT_TRUE(packet_map.isReceived(20003, 2064000 + 3 * kStep));
    ASSERT_TRUE(packet_map.isNotReceived(20004));
    ASSERT_TRUE(packet_map.isReceived(20005, 2064000 + 5 * kStep));
    ASSERT_TRUE(packet_map.isReceived(20006, 2064000 + 6 * kStep));
    ASSERT_TRUE(packet_map.isReceived(20007, 2064000 + 10 * kStep));
    ASSERT_TRUE(packet_map.isReceived(20008, 2064000 + 11 * kStep));
    ASSERT_TRUE(packet_map.isNotReceived(20009));
    ASSERT_TRUE(packet_map.isNotReceived(20010));
    ASSERT_TRUE(packet_map.isReceived(20011, 2064000 + 13 * kStep));
}

TEST(TWCCResponder, NotReceivedGap)
{
    constexpr auto kStep = 250; // microseconds

    const auto history = std::make_shared<srtc::twcc::SubscribePacketHistory>(1000000);

    history->saveIncomingPacket(20001, 3064000 + 1 * kStep);
    history->saveIncomingPacket(20002, 3064000 + 2 * kStep);
    history->saveIncomingPacket(20003, 3064000 + 3 * kStep);
    // A gap of not received
    history->saveIncomingPacket(20104, 3064000 + 4 * kStep);

    PacketMap packet_map;
    processReport(packet_map, history, 0);

    ASSERT_TRUE(packet_map.isReceived(20001, 2064000 + 1 * kStep));
    ASSERT_TRUE(packet_map.isReceived(20002, 2064000 + 2 * kStep));
    ASSERT_TRUE(packet_map.isReceived(20003, 2064000 + 3 * kStep));
    for (uint16_t seq = 20004; seq < 20104; seq += 1) {
        ASSERT_TRUE(packet_map.isNotReceived(seq));
    }
    ASSERT_TRUE(packet_map.isReceived(20104, 2064000 + 4 * kStep));
}

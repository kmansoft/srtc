#include <gtest/gtest.h>

#include "srtc/replay_protection.h"

namespace {
    constexpr uint32_t kSize = 2048;
}

// Replay protection

TEST(ReplayProtection, TestEmpty) {

    std::cout << "ReplayProtection TestEmpty" << std::endl;

    srtc::ReplayProtection replay_16(std::numeric_limits<uint16_t>::max(), kSize);
    {
        uint16_t value = 0;
        while (true) {
            ASSERT_TRUE(replay_16.canProceed(value));
            uint16_t newValue = value + 100;
            if (newValue < value) {
                break;
            }
            value = newValue;
        }
    }

    srtc::ReplayProtection replay_32(std::numeric_limits<uint32_t>::max(), kSize);
    {
        uint32_t value = 0;
        while (true) {
            ASSERT_TRUE(replay_32.canProceed(value));
            uint32_t newValue = value + 100;
            if (newValue < value || newValue >= std::numeric_limits<uint16_t>::max() * 10) {
                break;
            }
            value = newValue;
        }
    }
}

TEST(ReplayProtection, TestSimple1)
{
    srtc::ReplayProtection replay_16(std::numeric_limits<uint16_t>::max(), kSize);
    {
        uint16_t value = 10328;
        for (uint16_t i = 0; i < 20000; i += 1) {
            ASSERT_TRUE(replay_16.canProceed(value));
            ASSERT_TRUE(replay_16.set(value));
            ASSERT_FALSE(replay_16.canProceed(value));
            value += 1;
        }
    }
}

TEST(ReplayProtection, TestSimple2)
{
    srtc::ReplayProtection replay_16(std::numeric_limits<uint16_t>::max(), kSize);
    {
        uint16_t value = 12926;
        for (uint16_t i = 0; i < 20000; i += 1) {
            if (i >= 1) {
                ASSERT_TRUE(replay_16.canProceed(value - 1));
            }
            if (i >= 2) {
                ASSERT_FALSE(replay_16.canProceed(value - 2));
            }
            ASSERT_TRUE(replay_16.canProceed(value));
            ASSERT_TRUE(replay_16.set(value));
            ASSERT_FALSE(replay_16.canProceed(value));
            value += 2;
        }
    }
}

TEST(ReplayProtection, TestSimpleWithRollover)
{
    srtc::ReplayProtection replay_16(std::numeric_limits<uint16_t>::max(), kSize);
    {
        uint16_t value = 42926;
        for (uint16_t i = 0; ; i += 1) {
            if (i >= 1) {
                ASSERT_TRUE(replay_16.canProceed(
                        static_cast<uint16_t>(value - 1))) << "-1, value = " << value;
            }
            if (i >= 2) {
                ASSERT_FALSE(replay_16.canProceed(
                        static_cast<uint16_t>(value - 100))) << "-100, value = " << value;
            }
            ASSERT_TRUE(replay_16.canProceed(value));
            ASSERT_TRUE(replay_16.set(value));
            ASSERT_FALSE(replay_16.canProceed(value));
            value += 100;
            if (value >= 30000 && value < 40000) {
                break;
            }
        }
    }
}

TEST(ReplayProtection, TestTooMuchForwardSimple)
{
    srtc::ReplayProtection replay_16(std::numeric_limits<uint16_t>::max(), kSize);
    {
        uint16_t value = 42926;
        ASSERT_TRUE(replay_16.set(value));

        ASSERT_FALSE(replay_16.canProceed(value + kSize / 2));
        ASSERT_FALSE(replay_16.canProceed(value - kSize));

        ASSERT_TRUE(replay_16.canProceed(value + kSize / 4));
        ASSERT_TRUE(replay_16.canProceed(value - kSize + 1));
    }
}

TEST(ReplayProtection, TestRollover16)
{
    srtc::ReplayProtection replay_16(std::numeric_limits<uint16_t>::max(), kSize);
    {
        const auto value = std::numeric_limits<uint16_t>::max() - 100;
        ASSERT_TRUE(replay_16.set(value));

        ASSERT_FALSE(replay_16.canProceed(
                static_cast<uint16_t>(value + kSize / 2)));
        ASSERT_FALSE(replay_16.canProceed(
                static_cast<uint16_t>(value - kSize)));

        ASSERT_TRUE(replay_16.canProceed(
                static_cast<uint16_t>(value + kSize / 4)));
        ASSERT_TRUE(replay_16.canProceed(
                static_cast<uint16_t>(value - kSize + 1)));

        ASSERT_TRUE(replay_16.set(
                static_cast<uint16_t>(value + kSize / 4)));
        ASSERT_FALSE(replay_16.canProceed(
                static_cast<uint16_t>(value + kSize / 4)));
    }
}

TEST(ReplayProtection, TestRollover32)
{
    srtc::ReplayProtection replay_32(std::numeric_limits<uint32_t>::max(), kSize);
    {
        uint32_t value = std::numeric_limits<uint32_t>::max() - 100;
        ASSERT_TRUE(replay_32.set(value));

        ASSERT_FALSE(replay_32.canProceed(
                static_cast<uint32_t>(value + kSize / 2)));
        ASSERT_FALSE(replay_32.canProceed(
                static_cast<uint32_t>(value - kSize)));

        ASSERT_TRUE(replay_32.canProceed(
                static_cast<uint32_t>(value + kSize / 4)));
        ASSERT_TRUE(replay_32.canProceed(
                static_cast<uint32_t>(value - kSize + 1)));

        ASSERT_TRUE(replay_32.set(
                static_cast<uint32_t>(value + kSize / 4)));
        ASSERT_FALSE(replay_32.canProceed(
                static_cast<uint32_t>(value + kSize / 4)));
    }
}

#include <gtest/gtest.h>

#include "srtc/jitter_buffer.h"

#include <iostream>

// Extended value

TEST(ExtendedValue, TestSimple)
{
    std::cout << "ExtendedValue TestSimple" << std::endl;

    {
        srtc::ExtendedValue<uint16_t> e1;
        ASSERT_EQ(e1.extend(0xFF), 0x100FF);
    }

    {
        srtc::ExtendedValue<uint32_t> e2;
        ASSERT_EQ(e2.extend(0xFF), 0x1000000FFul);
    }

    {
        srtc::ExtendedValue<uint16_t> e3;
        ASSERT_EQ(e3.extend(0x100), 0x10100);
        ASSERT_EQ(e3.extend(0x101), 0x10101);
        ASSERT_EQ(e3.extend(0x102), 0x10102);
    }

    {
        srtc::ExtendedValue<uint32_t> e4;
        ASSERT_EQ(e4.extend(0x100), 0x100000100ul);
        ASSERT_EQ(e4.extend(0x101), 0x100000101ul);
        ASSERT_EQ(e4.extend(0x102), 0x100000102ul);
    }
}

TEST(ExtendedValue, TestRollover16)
{
    std::cout << "ExtendedValue TestRollover16" << std::endl;

    srtc::ExtendedValue<uint16_t> e1;

    ASSERT_EQ(e1.extend(0xFF00u), 0x1FF00ul);
    ASSERT_EQ(e1.extend(0xFF01u), 0x1FF01ul);
    ASSERT_EQ(e1.extend(0xFF02u), 0x1FF02ul);

    ASSERT_EQ(e1.extend(0x0010u), 0x20010ul);
    ASSERT_EQ(e1.extend(0x0011u), 0x20011ul);
    ASSERT_EQ(e1.extend(0x0012u), 0x20012ul);

    ASSERT_EQ(e1.extend(0xFF10u), 0x1FF10ul);
    ASSERT_EQ(e1.extend(0xFF11u), 0x1FF11ul);
    ASSERT_EQ(e1.extend(0xFF12u), 0x1FF12ul);

    ASSERT_EQ(e1.extend(0x0020u), 0x20020ul);
    ASSERT_EQ(e1.extend(0x0021u), 0x20021ul);
    ASSERT_EQ(e1.extend(0x0022u), 0x20022ul);

    ASSERT_EQ(e1.extend(0x4001u), 0x24001ul);
    ASSERT_EQ(e1.extend(0x4002u), 0x24002ul);
    ASSERT_EQ(e1.extend(0x4003u), 0x24003ul);

    ASSERT_EQ(e1.extend(0xFF01u), 0x2FF01ul);
    ASSERT_EQ(e1.extend(0xFF02u), 0x2FF02ul);
    ASSERT_EQ(e1.extend(0xFF03u), 0x2FF03ul);

    ASSERT_EQ(e1.extend(0x0001u), 0x30001ul);
    ASSERT_EQ(e1.extend(0x0002u), 0x30002ul);
    ASSERT_EQ(e1.extend(0x0003u), 0x30003ul);

    ASSERT_EQ(e1.extend(0xFF11u), 0x2FF11ul);
    ASSERT_EQ(e1.extend(0xFF12u), 0x2FF12ul);
    ASSERT_EQ(e1.extend(0xFF13u), 0x2FF13ul);
}

TEST(ExtendedValue, TestRollover32)
{
    std::cout << "ExtendedValue TestRollover32" << std::endl;

    srtc::ExtendedValue<uint32_t> e1;

    ASSERT_EQ(e1.extend(0xFFFFFF00u), 0x01FFFFFF00ul);
    ASSERT_EQ(e1.extend(0xFFFFFF01u), 0x01FFFFFF01ul);
    ASSERT_EQ(e1.extend(0xFFFFFF02u), 0x01FFFFFF02ul);

    ASSERT_EQ(e1.extend(0x00000010u), 0x200000010ul);
    ASSERT_EQ(e1.extend(0x00000011u), 0x200000011ul);
    ASSERT_EQ(e1.extend(0x00000012u), 0x200000012ul);

    ASSERT_EQ(e1.extend(0xFFFFFF10u), 0x1FFFFFF10ul);
    ASSERT_EQ(e1.extend(0xFFFFFF11u), 0x1FFFFFF11ul);
    ASSERT_EQ(e1.extend(0xFFFFFF12u), 0x1FFFFFF12ul);

    ASSERT_EQ(e1.extend(0x00000020u), 0x200000020ul);
    ASSERT_EQ(e1.extend(0x00000021u), 0x200000021ul);
    ASSERT_EQ(e1.extend(0x00000022u), 0x200000022ul);

    ASSERT_EQ(e1.extend(0x40000001u), 0x240000001ul);
    ASSERT_EQ(e1.extend(0x40000002u), 0x240000002ul);
    ASSERT_EQ(e1.extend(0x40000003u), 0x240000003ul);

    ASSERT_EQ(e1.extend(0xFF000001u), 0x2FF000001ul);
    ASSERT_EQ(e1.extend(0xFF000002u), 0x2FF000002ul);
    ASSERT_EQ(e1.extend(0xFF000003u), 0x2FF000003ul);

    ASSERT_EQ(e1.extend(0x00000001u), 0x300000001ul);
    ASSERT_EQ(e1.extend(0x00000002u), 0x300000002ul);
    ASSERT_EQ(e1.extend(0x00000003u), 0x300000003ul);

    ASSERT_EQ(e1.extend(0xFF000011u), 0x2FF000011ul);
    ASSERT_EQ(e1.extend(0xFF000012u), 0x2FF000012ul);
    ASSERT_EQ(e1.extend(0xFF000013u), 0x2FF000013ul);
}

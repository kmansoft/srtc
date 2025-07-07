#include <gtest/gtest.h>

#include "srtc/jitter_buffer.h"

#include <iostream>

// Extended value

TEST(ExtendedValue, TestSimple)
{
	std::cout << "ExtendedValue TestSimple" << std::endl;

	{
		srtc::ExtendedValue<uint16_t> e1;
		ASSERT_TRUE(e1.extend(0xFF).value() == 0xFF);
	}

	{
		srtc::ExtendedValue<uint32_t> e2;
		ASSERT_TRUE(e2.extend(0xFF).value() == 0xFF);
	}

	{
		srtc::ExtendedValue<uint16_t> e3;
		ASSERT_TRUE(e3.extend(0x100).value() == 0x100);
		ASSERT_TRUE(e3.extend(0x101).value() == 0x101);
		ASSERT_TRUE(e3.extend(0x102).value() == 0x102);
	}

	{
		srtc::ExtendedValue<uint32_t> e4;
		ASSERT_TRUE(e4.extend(0x100).value() == 0x100);
		ASSERT_TRUE(e4.extend(0x101).value() == 0x101);
		ASSERT_TRUE(e4.extend(0x102).value() == 0x102);
	}
}

TEST(ExtendedValue, TestRollover16)
{
	std::cout << "ExtendedValue TestRollover16" << std::endl;

	srtc::ExtendedValue<uint16_t> e1;

	ASSERT_TRUE(e1.extend(0xFF00u).value() == 0x0FF00ul);
	ASSERT_TRUE(e1.extend(0xFF01u).value() == 0x0FF01ul);
	ASSERT_TRUE(e1.extend(0xFF02u).value() == 0x0FF02ul);

	ASSERT_TRUE(e1.extend(0x0010u).value() == 0x10010ul);
	ASSERT_TRUE(e1.extend(0x0011u).value() == 0x10011ul);
	ASSERT_TRUE(e1.extend(0x0012u).value() == 0x10012ul);

	ASSERT_TRUE(e1.extend(0xFF10u).value() == 0x0FF10ul);
	ASSERT_TRUE(e1.extend(0xFF11u).value() == 0x0FF11ul);
	ASSERT_TRUE(e1.extend(0xFF12u).value() == 0x0FF12ul);

	ASSERT_TRUE(e1.extend(0x0020u).value() == 0x10020ul);
	ASSERT_TRUE(e1.extend(0x0021u).value() == 0x10021ul);
	ASSERT_TRUE(e1.extend(0x0022u).value() == 0x10022ul);

	ASSERT_TRUE(e1.extend(0x4001u).value() == 0x14001ul);
	ASSERT_TRUE(e1.extend(0x4002u).value() == 0x14002ul);
	ASSERT_TRUE(e1.extend(0x4003u).value() == 0x14003ul);

	ASSERT_TRUE(e1.extend(0xFF01u).value() == 0x1FF01ul);
	ASSERT_TRUE(e1.extend(0xFF02u).value() == 0x1FF02ul);
	ASSERT_TRUE(e1.extend(0xFF03u).value() == 0x1FF03ul);

	ASSERT_TRUE(e1.extend(0x0001u).value() == 0x20001ul);
	ASSERT_TRUE(e1.extend(0x0002u).value() == 0x20002ul);
	ASSERT_TRUE(e1.extend(0x0003u).value() == 0x20003ul);

	ASSERT_TRUE(e1.extend(0xFF11u).value() == 0x1FF11ul);
	ASSERT_TRUE(e1.extend(0xFF12u).value() == 0x1FF12ul);
	ASSERT_TRUE(e1.extend(0xFF13u).value() == 0x1FF13ul);
}

TEST(ExtendedValue, TestRollover32)
{
	std::cout << "ExtendedValue TestRollover32" << std::endl;

	srtc::ExtendedValue<uint32_t> e1;

	ASSERT_TRUE(e1.extend(0xFFFFFF00u).value() == 0xFFFFFF00ul);
	ASSERT_TRUE(e1.extend(0xFFFFFF01u).value() == 0xFFFFFF01ul);
	ASSERT_TRUE(e1.extend(0xFFFFFF02u).value() == 0xFFFFFF02ul);

	ASSERT_TRUE(e1.extend(0x00000010u).value() == 0x100000010ul);
	ASSERT_TRUE(e1.extend(0x00000011u).value() == 0x100000011ul);
	ASSERT_TRUE(e1.extend(0x00000012u).value() == 0x100000012ul);

	ASSERT_TRUE(e1.extend(0xFFFFFF10u).value() == 0x0FFFFFF10ul);
	ASSERT_TRUE(e1.extend(0xFFFFFF11u).value() == 0x0FFFFFF11ul);
	ASSERT_TRUE(e1.extend(0xFFFFFF12u).value() == 0x0FFFFFF12ul);


	ASSERT_TRUE(e1.extend(0x00000020u).value() == 0x100000020ul);
	ASSERT_TRUE(e1.extend(0x00000021u).value() == 0x100000021ul);
	ASSERT_TRUE(e1.extend(0x00000022u).value() == 0x100000022ul);

	ASSERT_TRUE(e1.extend(0x40000001u).value() == 0x140000001ul);
	ASSERT_TRUE(e1.extend(0x40000002u).value() == 0x140000002ul);
	ASSERT_TRUE(e1.extend(0x40000003u).value() == 0x140000003ul);

	ASSERT_TRUE(e1.extend(0xFF000001u).value() == 0x1FF000001ul);
	ASSERT_TRUE(e1.extend(0xFF000002u).value() == 0x1FF000002ul);
	ASSERT_TRUE(e1.extend(0xFF000003u).value() == 0x1FF000003ul);

	ASSERT_TRUE(e1.extend(0x00000001u).value() == 0x200000001ul);
	ASSERT_TRUE(e1.extend(0x00000002u).value() == 0x200000002ul);
	ASSERT_TRUE(e1.extend(0x00000003u).value() == 0x200000003ul);

	ASSERT_TRUE(e1.extend(0xFF000011u).value() == 0x1FF000011ul);
	ASSERT_TRUE(e1.extend(0xFF000012u).value() == 0x1FF000012ul);
	ASSERT_TRUE(e1.extend(0xFF000013u).value() == 0x1FF000013ul);
}

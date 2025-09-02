#pragma once

#include <cinttypes>
#include <cstddef>

namespace srtc
{

class BitReader
{
public:
    BitReader(const uint8_t* buffer, size_t size)
        : data(buffer)
        , dataSize(size)
        , bitPos(0)
    {
    }

    uint32_t readBit();
    uint32_t readBits(size_t n);
    uint32_t readUnsignedExpGolomb();

    const uint8_t* const data;
    const size_t dataSize;
    size_t bitPos;
};


}
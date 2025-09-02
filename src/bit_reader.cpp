#include "srtc/bit_reader.h"

namespace srtc
{

uint32_t BitReader::readBit()
{
    if ((bitPos >> 3) >= dataSize)
        return 0;

    uint8_t byte = data[bitPos >> 3];
    uint32_t bit = (byte >> (7 - (bitPos & 7))) & 1;
    bitPos++;
    return bit;
}

uint32_t BitReader::readBits(size_t n)
{
    uint32_t value = 0;
    for (size_t i = 0; i < n; i++) {
        value = (value << 1) | readBit();
    }
    return value;
}

uint32_t BitReader::readUnsignedExpGolomb()
{
    // Count leading zeros
    int leadingZeros = 0;
    while (readBit() == 0 && leadingZeros < 32) {
        leadingZeros++;
    }

    if (leadingZeros == 0) {
        return 0;
    }

    // Read remaining bits
    uint32_t remainingBits = readBits(leadingZeros);
    return (1 << leadingZeros) - 1 + remainingBits;
}

} // namespace srtc

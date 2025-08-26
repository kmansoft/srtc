#pragma once

#include "srtc/byte_buffer.h"

#include <cstdint>

namespace srtc::h265
{

struct NaluType {
    static constexpr uint8_t KeyFrame19 = 19;   // IDR_W_RADL
    static constexpr uint8_t KeyFrame20 = 20;   // IDR_N_LP
    static constexpr uint8_t KeyFrame21 = 21;   // CRA_NUT

    static constexpr uint8_t VPS = 32;
    static constexpr uint8_t SPS = 33;
    static constexpr uint8_t PPS = 34;
};

class NaluParser
{
public:
    explicit NaluParser(const ByteBuffer& buf);
    explicit operator bool() const;

    [[nodiscard]] bool isAtStart() const;
    [[nodiscard]] bool isAtEnd() const;

    void next();

    [[nodiscard]] uint8_t currType() const;
    [[nodiscard]] const uint8_t* currNalu() const;
    [[nodiscard]] size_t currNaluSize() const;
    [[nodiscard]] const uint8_t* currData() const;
    [[nodiscard]] size_t currDataSize() const;

private:
    const uint8_t* const mBuf;
    const size_t mSize;
    size_t mPos;
    size_t mSkip;
    size_t mNextPos;
    size_t mNextSkip;
};

//////////

bool isKeyFrame(uint8_t nalu_type);
bool isFrameStart(const uint8_t* frame, size_t size);

//////////

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

private:
    const uint8_t* const data;
    const size_t dataSize;
    size_t bitPos;
};

} // namespace srtc::h264


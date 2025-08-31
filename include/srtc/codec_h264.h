#pragma once

#include "srtc/byte_buffer.h"

#include <cstdint>

namespace srtc::h264
{

// https://datatracker.ietf.org/doc/html/rfc6184#section-5.7.1

constexpr uint8_t kPacket_STAP_A = 24;

// https://datatracker.ietf.org/doc/html/rfc6184#section-5.8

constexpr uint8_t kPacket_FU_A = 28;

// T-REC-H.264-201304-S

namespace NaluType
{
static constexpr uint8_t NonKeyFrame = 1;
static constexpr uint8_t KeyFrame = 5;
static constexpr uint8_t SEI = 6;
static constexpr uint8_t SPS = 7;
static constexpr uint8_t PPS = 8;
}; // namespace NaluType

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

bool isParameterNalu(uint8_t naluType);
bool isFrameStart(const uint8_t* nalu, size_t size);

bool isSliceFrameStart(const uint8_t* data, size_t size);

//////////

class BitReader
{
private:
    const uint8_t* const data;
    const size_t dataSize;
    size_t bitPos;

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
};

} // namespace srtc::h264

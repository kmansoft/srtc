#pragma once

#include "srtc/byte_buffer.h"

#include <cstdint>

namespace srtc::h265
{

// https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
constexpr uint8_t kPacket_AP = 48;

// https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.3
constexpr uint8_t kPacket_FU = 49;

namespace NaluType
{
static constexpr uint8_t KeyFrame19 = 19; // IDR_W_RADL
static constexpr uint8_t KeyFrame20 = 20; // IDR_N_LP
static constexpr uint8_t KeyFrame21 = 21; // CRA_NUT

static constexpr uint8_t VPS = 32;
static constexpr uint8_t SPS = 33;
static constexpr uint8_t PPS = 34;
}; // namespace NaluType

//////////

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
bool isKeyFrameNalu(uint8_t naluType);
bool isFrameStart(const uint8_t* nalu, size_t size);

bool isSliceNalu(uint8_t naluType);
bool isSliceFrameStart(const uint8_t* data, size_t size);

} // namespace srtc::h265

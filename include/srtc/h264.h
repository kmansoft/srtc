#pragma once

#include "srtc/byte_buffer.h"

#include <cstdint>

namespace srtc::h264
{

// https://datatracker.ietf.org/doc/html/rfc6184#section-5.4

constexpr uint8_t STAP_A = 24;
constexpr uint8_t FU_A = 28;

// T-REC-H.264-201304-S

enum class NaluType : uint8_t {
    NonKeyFrame = 1,
    KeyFrame = 5,
    SEI = 6,
    SPS = 7,
    PPS = 8
};

class NaluParser
{
public:
    explicit NaluParser(const ByteBuffer& buf);
    explicit operator bool() const;

    [[nodiscard]] bool isAtStart() const;
    [[nodiscard]] bool isAtEnd() const;

    void next();

    [[nodiscard]] uint8_t currRefIdc() const;
    [[nodiscard]] NaluType currType() const;
    [[nodiscard]] const uint8_t* currNalu() const;
    [[nodiscard]] size_t currNaluSize() const;
    [[nodiscard]] const uint8_t* currData() const;
    [[nodiscard]] size_t currDataSize() const;

private:
    const uint8_t* const mBuf;
    const size_t mSize;
    size_t mPos;
    size_t mNext;
    size_t mSkip;
};

} // namespace srtc::h264


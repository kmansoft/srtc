#pragma once

#pragma once

#include "srtc/byte_buffer.h"

#include <cstdint>

namespace srtc::av1
{

namespace ObuType
{
// https://aomediacodec.github.io/av1-spec/#obu-header-semantics

static constexpr uint8_t SequenceHeader = 1;
static constexpr uint8_t TemporalDelimiter = 2;
static constexpr uint8_t FrameHeader = 3;
static constexpr uint8_t Frame = 6;
static constexpr uint8_t RedundantFrame = 7;
}; // namespace ObuType

class ObuParser
{
public:
    explicit ObuParser(const ByteBuffer& buf);
    explicit operator bool() const;

    void next();

    [[nodiscard]] bool isAtEnd() const;

    [[nodiscard]] uint8_t currType() const;
    [[nodiscard]] const uint8_t* currData() const;
    [[nodiscard]] size_t currSize() const;
    [[nodiscard]] uint8_t currTemporalId() const;
    [[nodiscard]] uint8_t currSpatialId() const;

private:
    const uint8_t* const mData;
    const uint8_t* const mEnd;

    uint8_t mCurrType;
    const uint8_t* mCurrData;
    size_t mCurrSize;
    uint8_t mCurrTemporalId;
    uint8_t mCurrSpatialId;

    void parseImpl(const uint8_t* data, size_t size);
};

//////////

bool isFrameObuType(uint8_t obuType);
bool isKeyFrameObu(uint8_t obuType, const uint8_t* data, size_t size);

} // namespace srtc::av1

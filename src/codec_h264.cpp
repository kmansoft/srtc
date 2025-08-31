#include "srtc/codec_h264.h"

namespace
{

bool is_nalu_4(const uint8_t* buf, size_t pos, size_t end)
{
    if (pos < end - 4 && buf[pos + 0] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 0 && buf[pos + 3] == 1) {
        return true;
    }

    return false;
}

bool is_nalu_3(const uint8_t* buf, size_t pos, size_t end)
{
    if (pos < end - 3 && buf[pos + 0] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1) {
        return true;
    }

    return false;
}

size_t find_next_nalu(const uint8_t* buf, size_t pos, size_t end, size_t& out_skip)
{
    while (pos < end) {
        if (is_nalu_4(buf, pos, end)) {
            out_skip = 4;
            return pos;
        } else if (is_nalu_3(buf, pos, end)) {
            out_skip = 3;
            return pos;
        }
        pos += 1;
    }

    out_skip = 0;
    return end;
}

} // namespace

namespace srtc::h264
{

NaluParser::NaluParser(const ByteBuffer& buf)
    : mBuf(buf.data())
    , mSize(buf.size())
{
    mSkip = 0;
    mNextSkip = 0;

    mPos = find_next_nalu(mBuf, 0, mSize, mSkip);
    mNextPos = find_next_nalu(mBuf, mPos + mSkip, mSize, mNextSkip);
}

NaluParser::operator bool() const
{
    return mPos < mSize;
}

bool NaluParser::isAtStart() const
{
    return mPos == 0;
}

bool NaluParser::isAtEnd() const
{
    return mNextPos >= mSize;
}

void NaluParser::next()
{
    mPos = mNextPos;
    mSkip = mNextSkip;

    mNextPos = find_next_nalu(mBuf, mNextPos + mSkip, mSize, mNextSkip);
}

uint8_t NaluParser::currType() const
{
    return mBuf[mPos + mSkip] & 0x1F;
}

const uint8_t* NaluParser::currNalu() const
{
    return mBuf + mPos;
}

size_t NaluParser::currNaluSize() const
{
    return mNextPos - mPos;
}

const uint8_t* NaluParser::currData() const
{
    return mBuf + mPos + mSkip;
}

size_t NaluParser::currDataSize() const
{
    return mNextPos - mPos - mSkip;
}

//////////

bool isParameterNalu(uint8_t naluType)
{
    return naluType == NaluType::SPS || naluType == NaluType::PPS;
}

bool isKeyFrameNalu(uint8_t naluType)
{
    return naluType == NaluType::KeyFrame;
}

bool isFrameStart(const uint8_t* nalu, size_t size)
{
    if (size > 0) {
        const auto naluType = nalu[0] & 0x1F;
        if (naluType == NaluType::KeyFrame || naluType == NaluType::NonKeyFrame) {
            if (size > 1) {
                BitReader reader(nalu + 1, size - 1);
                return reader.readUnsignedExpGolomb() == 0;
            }
        }
    }

    return false;
}

bool isSliceNalu(uint8_t naluType)
{
    return naluType == NaluType::NonKeyFrame || naluType == NaluType::KeyFrame;
}

bool isSliceFrameStart(const uint8_t* data, size_t size)
{
    if (size > 0) {
        BitReader reader(data, size);
        return reader.readUnsignedExpGolomb() == 0;
    }
    return false;
}

//////////

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

} // namespace srtc::h264

#include "srtc/h264.h"

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

size_t find_next_nalu(const uint8_t* buf, size_t pos, size_t end)
{
    while (pos < end) {
        if (is_nalu_4(buf, pos, end)) {
            return pos;
        } else if (is_nalu_3(buf, pos, end)) {
            return pos;
        }
        pos += 1;
    }

    return end;
}

} // namespace

namespace srtc
{
namespace h264
{

NaluParser::NaluParser(const ByteBuffer& buf)
    : mBuf(buf.data())
    , mSize(buf.size())
    , mPos(find_next_nalu(mBuf, 0, mSize))
{
    if (is_nalu_4(mBuf, mPos, mSize)) {
        mSkip = 4;
    } else {
        mSkip = 3;
    }
    mNext = find_next_nalu(mBuf, mPos + mSkip, mSize);
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
    return mNext >= mSize;
}

void NaluParser::next()
{
    mPos = mNext;
    if (is_nalu_4(mBuf, mPos, mSize)) {
        mSkip = 4;
    } else {
        mSkip = 3;
    }
    mNext = find_next_nalu(mBuf, mNext + mSkip, mSize);
}

uint8_t NaluParser::currRefIdc() const
{
    return mBuf[mPos + mSkip] >> 5;
}

NaluType NaluParser::currType() const
{
    return static_cast<NaluType>(mBuf[mPos + mSkip] & 0x1F);
}

const uint8_t* NaluParser::currNalu() const
{
    return mBuf + mPos;
}

size_t NaluParser::currNaluSize() const
{
    return mNext - mPos;
}

const uint8_t* NaluParser::currData() const
{
    return mBuf + mPos + mSkip;
}

size_t NaluParser::currDataSize() const
{
    return mNext - mPos - mSkip;
}

} // namespace h264
} // namespace srtc

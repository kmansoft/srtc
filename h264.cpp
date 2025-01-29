#include "srtc/h264.h"

namespace {

size_t find_next_nalu(const uint8_t* buf, size_t pos, size_t end)
{
    while (pos < end - 4) {
        if (buf[pos + 0] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 0 && buf[pos + 3] == 1) {
            return pos;
        }
        pos += 1;
    }

    return end;
}

}

namespace srtc {
namespace h264 {

NaluParser::NaluParser(const ByteBuffer &buf)
        : mBuf(buf.data()), mSize(buf.size()), mPos(find_next_nalu(mBuf, 0, mSize)),
          mNext(find_next_nalu(mBuf, mPos + 4, mSize))
{
}

NaluParser::operator bool() const
{
    return mPos < mSize;
}

bool NaluParser::isAtStart() const
{
    return mPos == 0;
}

void NaluParser::next()
{
    mPos = mNext;
    mNext = find_next_nalu(mBuf, mNext + 4, mSize);
}

uint8_t NaluParser::currRefIdc() const
{
    return mBuf[mPos + 4] >> 5;
}

NaluType NaluParser::currType() const
{
    return static_cast<NaluType>(mBuf[mPos + 4] & 0x1F);
}

const uint8_t *NaluParser::currNalu() const
{
    return mBuf + mPos;
}

size_t NaluParser::currNaluSize() const
{
    return mNext - mPos;
}

const uint8_t *NaluParser::currData() const
{
    return mBuf + mPos + 4;
}

size_t NaluParser::currDataSize() const
{
    return mNext - mPos - 4;
}

}
}

#include "srtc/codec_h265.h"

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

namespace srtc::h265
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
    return (mBuf[mPos + mSkip] >> 1) & 0x3F;
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
    return naluType == NaluType::VPS || naluType == NaluType::SPS || naluType == NaluType::PPS;
}

bool isKeyFrameNalu(uint8_t nalu_type)
{
    return nalu_type == NaluType::KeyFrame19 || nalu_type == NaluType::KeyFrame20 || nalu_type == NaluType::KeyFrame21;
}

bool isFrameStart(const uint8_t* frame, size_t size)
{
    if (size < 3) {
        return false;
    }

    const auto nalu_type = (frame[0] >> 1) & 0x3F;
    if (nalu_type <= 21) {
        // Regular slice - check first_slice_segment_in_pic_flag
        return (frame[2] & 0x80) != 0;
    } else {
        // Non-slice NAL unit
        return false;
    }
}
} // namespace srtc::h265

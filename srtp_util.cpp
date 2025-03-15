#include "srtc/srtp_util.h"

#include <cstring>
#include <cassert>

namespace srtc {

CryptoBytes::CryptoBytes()
    : mSize(0)
{
}

void CryptoBytes::clear()
{
    std::memset(v8, 0, sizeof(v8));
    mSize = 0;
}

void CryptoBytes::assign(const uint8_t* ptr, size_t size)
{
    assert(size <= sizeof(v8));

    mSize = std::min(sizeof(v8), size);

    size_t i = 0;
    while (i < mSize) {
        v8[i] = ptr[i];
        i += 1;
    }

    while (i < sizeof(v8)) {
        v8[i] = 0;
        i += 1;
    }
}

uint8_t* CryptoBytes::data()
{
    return v8;
}

const uint8_t* CryptoBytes::data() const
{
    return v8;
}

bool CryptoBytes::empty() const
{
    return mSize == 0;
}

size_t CryptoBytes::size() const
{
    return mSize;
}

CryptoBytes& CryptoBytes::operator^=(const CryptoBytes& other)
{
    for (size_t i = 0; i < sizeof(v8); i += 1) {
        v8[i] ^= other.v8[i];
    }

    return *this;
}

CryptoBytesWriter::CryptoBytesWriter(CryptoBytes& bytes)
    : mBytes(bytes)
{
    mBytes.clear();
}

void CryptoBytesWriter::writeU8(uint8_t value)
{
    assert(mBytes.mSize <= sizeof(mBytes.v8) - 1);
    if (mBytes.mSize <= sizeof(mBytes.v8) - 1) {
        mBytes.v8[mBytes.mSize++] = value;
    }
}

void CryptoBytesWriter::writeU16(uint16_t value)
{
    assert(mBytes.mSize <= sizeof(mBytes.v8) - 2);
    if (mBytes.mSize <= sizeof(mBytes.v8) - 2) {
        mBytes.v8[mBytes.mSize++] = (value >> 8) & 0xff;
        mBytes.v8[mBytes.mSize++] = value & 0xff;
    }
}

void CryptoBytesWriter::writeU32(uint32_t value)
{
    assert(mBytes.mSize <= sizeof(mBytes.v8) - 4);
    if (mBytes.mSize <= sizeof(mBytes.v8) - 4) {
        mBytes.v8[mBytes.mSize++] = (value >> 24) & 0xff;
        mBytes.v8[mBytes.mSize++] = (value >> 16) & 0xff;
        mBytes.v8[mBytes.mSize++] = (value >> 8) & 0xff;
        mBytes.v8[mBytes.mSize++] = value & 0xff;
    }
}

void CryptoBytesWriter::append(const uint8_t* data, size_t size)
{
    assert(mBytes.mSize <= sizeof(mBytes.v8) - size);
    if (mBytes.mSize <= sizeof(mBytes.v8) - size) {
        std::memcpy(mBytes.v8 + mBytes.mSize, data, size);
        mBytes.mSize += size;
    }
}

}
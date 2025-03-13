#include "srtc/srtp_util.h"

#include <cstring>
#include <cassert>

namespace srtc {

CryptoBytes::CryptoBytes()
{
}

void CryptoBytes::clear()
{
    std::memset(v8, 0, sizeof(v8));
}

void CryptoBytes::assign(const uint8_t* ptr, size_t size)
{
    assert(size <= sizeof(v8));

    size_t i = 0;
    while (i < std::min(sizeof(v8), size)) {
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

CryptoBytes& CryptoBytes::operator^=(const CryptoBytes& other)
{
    for (size_t i = 0; i < sizeof(v8); i += 1) {
        v8[i] ^= other.v8[i];
    }

    return *this;
}


CryptoBytesWriter::CryptoBytesWriter(CryptoBytes& bytes)
    : mBytes(bytes)
    , mPos(0)
{
    mBytes.clear();
}

void CryptoBytesWriter::writeU8(uint8_t value)
{
    assert(mPos <= sizeof(mBytes.v8) - 1);
    if (mPos <= sizeof(mBytes.v8) - 1) {
        mBytes.v8[mPos++] = value;
    }
}

void CryptoBytesWriter::writeU16(uint16_t value)
{
    assert(mPos <= sizeof(mBytes.v8) - 2);
    if (mPos <= sizeof(mBytes.v8) - 2) {
        mBytes.v8[mPos++] = (value >> 8) & 0xff;
        mBytes.v8[mPos++] = value & 0xff;
    }
}

void CryptoBytesWriter::writeU32(uint32_t value)
{
    assert(mPos <= sizeof(mBytes.v8) - 4);
    if (mPos <= sizeof(mBytes.v8) - 4) {
        mBytes.v8[mPos++] = (value >> 24) & 0xff;
        mBytes.v8[mPos++] = (value >> 16) & 0xff;
        mBytes.v8[mPos++] = (value >> 8) & 0xff;
        mBytes.v8[mPos++] = value & 0xff;
    }
}

}
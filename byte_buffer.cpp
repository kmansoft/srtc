#include "srtc/byte_buffer.h"

#include <algorithm>

namespace srtc {

// ----- ByteBuffer

ByteBuffer::ByteBuffer()
    : mBuf(nullptr)
    , mLen(0)
    , mCap(0)
{
}

ByteBuffer::ByteBuffer(const uint8_t* src,
                       size_t size)
    : mBuf(size == 0 ? nullptr : new uint8_t[size])
    , mLen(size)
    , mCap(size)
{
    if (size > 0) {
        std::memcpy(mBuf, src, size);
    }
}

ByteBuffer::~ByteBuffer()
{
    delete[] mBuf;
    mBuf = nullptr;
}

ByteBuffer::ByteBuffer(ByteBuffer&& other)
    : mBuf(other.mBuf)
    , mLen(other.mLen)
    , mCap(other.mCap)
{
    other.mBuf = nullptr;
    other.mLen = 0;
    other.mCap = 0;
}

ByteBuffer& ByteBuffer::operator=(ByteBuffer&& other)
{
    delete[] mBuf;

    mBuf = other.mBuf;
    mLen = other.mLen;
    mCap = other.mCap;

    other.mBuf = nullptr;
    other.mLen = 0;
    other.mCap = 0;

    return *this;
}

bool ByteBuffer::empty() const
{
    return mLen == 0;
}

void ByteBuffer::clear()
{
    delete[] mBuf;
    mLen = 0;
    mCap = 0;
}

void ByteBuffer::reserve(size_t size)
{
    ensureCapacity(size);
}

void ByteBuffer::append(const uint8_t* src, size_t size)
{
    ensureCapacity(mLen + size);

    std::memcpy(mBuf + mLen, src, size);
    mLen += size;
}

void ByteBuffer::append(const ByteBuffer& buf)
{
    if (buf.mLen > 0) {
        append(buf.mBuf, buf.mLen);
    }
}

void ByteBuffer::padding(size_t size)
{
    ensureCapacity(mLen + size);

    std::memset(mBuf + mLen, 0, size);
    mLen += size;
}

uint8_t* ByteBuffer::data() const
{
    return mBuf;
}

size_t ByteBuffer::size() const
{
    return mLen;
}

ByteBuffer ByteBuffer::copy() const
{
    return { mBuf, mLen };
}

bool ByteBuffer::operator==(const ByteBuffer& other) const
{
    if (this->mLen == other.mLen) {
        return std::memcmp(this->mBuf, other.mBuf, this->mLen) == 0;
    }
    return false;
}

void ByteBuffer::ensureCapacity(size_t capacity)
{
    if (capacity > mCap) {
        const auto newCap = std::max(capacity + 128, mCap * 3 / 2);
        const auto newBuf = new uint8_t[newCap];

        std::memcpy(newBuf, mBuf, mLen);

        delete[] mBuf;
        mBuf = newBuf;
        mCap = newCap;
    }
}

// ----- ByteWriter

ByteWriter::ByteWriter(ByteBuffer& buf)
    : mBuf(buf)
{
}

void ByteWriter::write(const uint8_t* value,
                       size_t size)
{
    mBuf.append(value, size);
}

void ByteWriter::writeU8(uint8_t value)
{
    const uint8_t buf[] = { value };
    mBuf.append(buf, sizeof(buf));
}

void ByteWriter::writeU16(uint16_t value)
{
    const uint8_t buf[] = {
            static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value & 0xFF) };
    mBuf.append(buf, sizeof(buf));
}

void ByteWriter::writeU24(uint32_t value)
{
    const uint8_t buf[] = {
            static_cast<uint8_t>(value >> 16),
            static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value & 0xFF) };
    mBuf.append(buf, sizeof(buf));
}

void ByteWriter::writeU32(uint32_t value)
{
    const uint8_t buf[] = {
            static_cast<uint8_t>(value >> 24),
            static_cast<uint8_t>(value >> 16),
            static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value & 0xFF) };
    mBuf.append(buf, sizeof(buf));
}

void ByteWriter::writeU48(uint64_t value)
{
    const uint8_t buf[] = {
            static_cast<uint8_t>(value >> 40),
            static_cast<uint8_t>(value >> 32),
            static_cast<uint8_t>(value >> 24),
            static_cast<uint8_t>(value >> 16),
            static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value & 0xFF) };
    mBuf.append(buf, sizeof(buf));
}

void ByteWriter::writeU64(uint64_t value)
{
    const uint8_t buf[] = {
            static_cast<uint8_t>(value >> 56),
            static_cast<uint8_t>(value >> 48),
            static_cast<uint8_t>(value >> 40),
            static_cast<uint8_t>(value >> 32),
            static_cast<uint8_t>(value >> 24),
            static_cast<uint8_t>(value >> 16),
            static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value & 0xFF) };
    mBuf.append(buf, sizeof(buf));
}

}

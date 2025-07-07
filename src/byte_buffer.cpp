#include "srtc/byte_buffer.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace srtc
{

// ----- ByteBuffer

ByteBuffer::ByteBuffer()
	: mBuf(nullptr)
	, mLen(0)
	, mCap(0)
{
}

ByteBuffer::ByteBuffer(size_t size)
	: mBuf(size == 0 ? nullptr : new uint8_t[size])
	, mLen(0)
	, mCap(size)
{
}

ByteBuffer::ByteBuffer(const uint8_t* src, size_t size)
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

ByteBuffer::ByteBuffer(ByteBuffer&& other) noexcept
	: mBuf(other.mBuf)
	, mLen(other.mLen)
	, mCap(other.mCap)
{
	other.mBuf = nullptr;
	other.mLen = 0;
	other.mCap = 0;
}

ByteBuffer& ByteBuffer::operator=(ByteBuffer&& other) noexcept
{
	if (this != &other) {
		delete[] mBuf;

		mBuf = other.mBuf;
		mLen = other.mLen;
		mCap = other.mCap;

		other.mBuf = nullptr;
		other.mLen = 0;
		other.mCap = 0;
	}

	return *this;
}

bool ByteBuffer::empty() const
{
	return mLen == 0;
}

void ByteBuffer::clear()
{
	mLen = 0;
}

void ByteBuffer::free()
{
	delete[] mBuf;
	mBuf = nullptr;
	mLen = 0;
	mCap = 0;
}

void ByteBuffer::assign(const uint8_t* src, size_t size)
{
	delete[] mBuf;
	if (size) {
		mBuf = new uint8_t[size];
		std::memcpy(mBuf, src, size);
	} else {
		mBuf = nullptr;
	}
	mLen = size;
	mCap = size;
}

void ByteBuffer::resize(size_t size)
{
	assert(size <= mCap);
	mLen = size;
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

void ByteBuffer::padding(uint8_t c, size_t size)
{
	ensureCapacity(mLen + size);

	std::memset(mBuf + mLen, c, size);
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

size_t ByteBuffer::capacity() const
{
	return mCap;
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

void ByteWriter::write(const ByteBuffer& value)
{
	mBuf.append(value.data(), value.size());
}

void ByteWriter::write(const uint8_t* value, size_t size)
{
	mBuf.append(value, size);
}

void ByteWriter::padding(uint8_t c, size_t size)
{
	mBuf.padding(c, size);
}

void ByteWriter::writeU8(uint8_t value)
{
	const uint8_t buf[] = { value };
	mBuf.append(buf, sizeof(buf));
}

void ByteWriter::writeU16(uint16_t value)
{
	const uint8_t buf[] = { static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value) };
	mBuf.append(buf, sizeof(buf));
}

void ByteWriter::writeU24(uint32_t value)
{
	const uint8_t buf[] = { static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value) };
	mBuf.append(buf, sizeof(buf));
}

void ByteWriter::writeU32(uint32_t value)
{
	const uint8_t buf[] = {
		static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)
	};
	mBuf.append(buf, sizeof(buf));
}

void ByteWriter::writeU48(uint64_t value)
{
	const uint8_t buf[] = { static_cast<uint8_t>(value >> 40), static_cast<uint8_t>(value >> 32), static_cast<uint8_t>(value >> 24),
							static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8),  static_cast<uint8_t>(value) };
	mBuf.append(buf, sizeof(buf));
}

void ByteWriter::writeU64(uint64_t value)
{
	const uint8_t buf[] = { static_cast<uint8_t>(value >> 56), static_cast<uint8_t>(value >> 48), static_cast<uint8_t>(value >> 40),
							static_cast<uint8_t>(value >> 32), static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
							static_cast<uint8_t>(value >> 8),  static_cast<uint8_t>(value) };
	mBuf.append(buf, sizeof(buf));
}

void ByteWriter::writeLEB128(uint32_t value)
{
	auto remaining = value;
	while (true) {
		const auto sevenBits = remaining & 0x7F;
		remaining >>= 7;

		if (remaining == 0) {
			writeU8(sevenBits);
			break;
		} else {
			writeU8(0x80 | sevenBits);
		}
	}
}

// ByteReader

ByteReader::ByteReader(const ByteBuffer& buf)
	: mBuf(buf.data())
	, mLen(buf.size())
	, mPos(0)
{
}

ByteReader::ByteReader(const ByteBuffer& buf, size_t size)
	: mBuf(buf.data())
	, mLen(size)
	, mPos(0)
{
	assert(mLen <= buf.size());
}

ByteReader::ByteReader(const uint8_t* buf, size_t size)
	: mBuf(buf)
	, mLen(size)
	, mPos(0)
{
}

size_t ByteReader::size() const
{
	return mLen;
}

size_t ByteReader::position() const
{
	return mPos;
}

size_t ByteReader::remaining() const
{
	if (mLen <= mPos) {
		return 0;
	}
	return mLen - mPos;
}

uint8_t ByteReader::readU8()
{
	assert(mPos + 1 <= mLen);
	const uint8_t res = mBuf[mPos];
	mPos += 1;
	return res;
}

uint16_t ByteReader::readU16()
{
	assert(mPos + 2 <= mLen);
	const uint16_t res = (static_cast<uint16_t>(mBuf[mPos]) << 8) | static_cast<uint16_t>(mBuf[mPos + 1]);
	mPos += 2;
	return res;
}

uint32_t ByteReader::readU32()
{
	assert(mPos + 4 <= mLen);
	const uint32_t res = (static_cast<uint32_t>(mBuf[mPos]) << 24) | (static_cast<uint32_t>(mBuf[mPos + 1]) << 16) |
						 (static_cast<uint32_t>(mBuf[mPos + 2]) << 8) | static_cast<uint32_t>(mBuf[mPos + 3]);
	mPos += 4;
	return res;
}

ByteBuffer ByteReader::readByteBuffer(size_t size)
{
	assert(mPos + size <= mLen);

	ByteBuffer res(size);
	std::memcpy(res.data(), mBuf + mPos, size);
	res.resize(size);

	mPos += size;
	return res;
}

void ByteReader::read(uint8_t* buf, size_t size)
{
	assert(mPos + size <= mLen);

	std::memcpy(buf, mBuf + mPos, size);

	mPos += size;
}

void ByteReader::skip(size_t size)
{
	mPos += size;
}

} // namespace srtc

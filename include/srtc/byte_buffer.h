#pragma once

#include <cinttypes>
#include <cstddef>

namespace srtc {

// ByteBuffer

class ByteBuffer {
public:
    ByteBuffer();
    explicit ByteBuffer(size_t size);
    ByteBuffer(const uint8_t* src,
                        size_t size);

    ~ByteBuffer();

    ByteBuffer(ByteBuffer&& other) noexcept;
    ByteBuffer& operator=(ByteBuffer&& other) noexcept;

    ByteBuffer(const ByteBuffer& other) = delete;
    void operator=(const ByteBuffer& other) = delete;

    [[nodiscard]] bool empty() const;

    void clear();
    void assign(const uint8_t* src, size_t size);
    void resize(size_t size);
    void reserve(size_t size);
    void append(const uint8_t* src, size_t size);
    void append(const ByteBuffer& buf);
    void padding(size_t size);

    [[nodiscard]] uint8_t* data() const;
    [[nodiscard]] size_t size() const;
    [[nodiscard]] size_t capacity() const;

    [[nodiscard]] ByteBuffer copy() const;

    [[nodiscard]] bool operator==(const ByteBuffer& other) const;

private:
    void ensureCapacity(size_t capacity);

    uint8_t* mBuf;
    size_t mLen;
    size_t mCap;
};

// ByteWriter

class ByteWriter {
public:
    explicit ByteWriter(ByteBuffer& buf);

    void write(const uint8_t* value,
               size_t size);
    void write(const ByteBuffer& value);

    void writeU8(uint8_t value);
    void writeU16(uint16_t value);
    void writeU24(uint32_t value);
    void writeU32(uint32_t value);
    void writeU48(uint64_t value);
    void writeU64(uint64_t value);
    void writeLEB128(uint32_t value);

private:
    ByteBuffer& mBuf;
};

// ByteReader

class ByteReader {
public:
    explicit ByteReader(const ByteBuffer& buf);
    ByteReader(const ByteBuffer& buf, size_t size);
    ByteReader(const uint8_t* buf, size_t size);

    [[nodiscard]] size_t current() const;
    [[nodiscard]] size_t remaining() const;

    [[nodiscard]] uint8_t readU8();
    [[nodiscard]] uint16_t readU16();
    [[nodiscard]] uint32_t readU32();

    void skip(size_t size);

private:
    const uint8_t* mBuf;
    size_t mLen;
    size_t mPos;
};

}

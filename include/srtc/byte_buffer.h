#pragma once

#include <cinttypes>

namespace srtc {

class ByteBuffer {
public:
    ByteBuffer();
    ByteBuffer(const uint8_t* src,
               size_t size);

    ~ByteBuffer();

    ByteBuffer(const ByteBuffer& other) = delete;
    void operator=(const ByteBuffer& other) = delete;

    ByteBuffer(ByteBuffer&& other);
    ByteBuffer& operator=(ByteBuffer&& other);

    [[nodiscard]] bool empty() const;

    void clear();
    void reserve(size_t size);
    void append(const uint8_t* src, size_t size);
    void append(const ByteBuffer& buf);
    void padding(size_t size);

    [[nodiscard]] uint8_t* data() const;
    [[nodiscard]] size_t size() const;

    [[nodiscard]] ByteBuffer copy() const;

    [[nodiscard]] bool operator==(const ByteBuffer& other) const;

private:
    void ensureCapacity(size_t capacity);

    uint8_t* mBuf;
    size_t mLen;
    size_t mCap;
};

class ByteWriter {
public:
    explicit ByteWriter(ByteBuffer& buf);

    void write(const uint8_t* value,
               size_t size);

    void writeU8(uint8_t value);
    void writeU16(uint16_t value);
    void writeU24(uint32_t value);
    void writeU32(uint32_t value);
    void writeU48(uint64_t value);
    void writeU64(uint64_t value);

private:
    ByteBuffer& mBuf;
};

}

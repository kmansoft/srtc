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

    void clear();
    void append(const uint8_t* src, size_t size);
    void append(const ByteBuffer& buf);

    [[nodiscard]] uint8_t* data() const;
    [[nodiscard]] size_t len() const;

    [[nodiscard]] ByteBuffer copy() const;

    [[nodiscard]] bool operator==(const ByteBuffer& other) const;

private:
    uint8_t* mBuf;
    size_t mLen;
    size_t mCap;
};

class ByteWriter {
public:
    ByteWriter(ByteBuffer& buf);

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

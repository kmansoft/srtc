#pragma once

#include <memory>
#include <cstdint>

namespace srtc {

class CryptoBytesWriter;

class CryptoBytes {
public:
    // Enough for AES 128 to AES 256 key sizes
    uint8_t v8[32] = { };

    CryptoBytes();

    void clear();
    void assign(const uint8_t* ptr, size_t size);

    [[nodiscard]] uint8_t* data();
    [[nodiscard]] const uint8_t* data() const;

    [[nodiscard]] bool empty() const;
    [[nodiscard]] size_t size() const;

    CryptoBytes& operator^=(const CryptoBytes& other);

private:
    friend CryptoBytesWriter;

    size_t mSize;
};

class CryptoBytesWriter {
public:
    CryptoBytesWriter(CryptoBytes& bytes);

    void writeU8(uint8_t value);
    void writeU16(uint16_t value);
    void writeU32(uint32_t value);

    void append(const uint8_t* data, size_t size);

private:
    CryptoBytes& mBytes;
};

}
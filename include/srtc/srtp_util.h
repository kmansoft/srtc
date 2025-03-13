#pragma once

#include <memory>
#include <cstdint>

namespace srtc {

class CryptoBytes {
public:
    union {
        // Enough for AES 128 to AES 256 key sizes
        uint8_t v8[32] = { };
        uint16_t v16[16];
        uint32_t v32[8];
    };

    CryptoBytes();

    void clear();
    void assign(const uint8_t* ptr, size_t size);

    [[nodiscard]] uint8_t* data();
    [[nodiscard]] const uint8_t* data() const;

    CryptoBytes& operator^=(const CryptoBytes& other);
};

class CryptoBytesWriter {
public:
    CryptoBytesWriter(CryptoBytes& bytes);

    void writeU8(uint8_t value);
    void writeU16(uint16_t value);
    void writeU32(uint16_t value);

private:
    CryptoBytes& mBytes;
    size_t mPos;
};

}
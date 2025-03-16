#pragma once

#include <memory>
#include <cstdint>

namespace srtc {

class CryptoBytesWriter;

// ----- CryptoBytes

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

// ----- CryptoBytesWriter

class CryptoBytesWriter {
public:
    explicit CryptoBytesWriter(CryptoBytes& bytes);

    void writeU8(uint8_t value);
    void writeU16(uint16_t value);
    void writeU32(uint32_t value);

    void append(const uint8_t* data, size_t size);

private:
    CryptoBytes& mBytes;
};

// ----- KeyDerivation

class KeyDerivation {
public:
    static constexpr uint8_t kLabelRtpKey = 0;
    static constexpr uint8_t kLabelRtpSalt = 2;
    static constexpr uint8_t kLabelRtcpKey = 3;
    static constexpr uint8_t kLabelRtcpAuth = 4;
    static constexpr uint8_t kLabelRtcpSalt = 5;

    [[nodiscard]] static bool generate(const CryptoBytes& masterKey,
                                       const CryptoBytes& masterSalt,
                                       uint8_t label,
                                       srtc::CryptoBytes& output,
                                       size_t desiredOutputSize);
};

}

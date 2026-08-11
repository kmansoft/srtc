#pragma once

#include "srtc/byte_buffer.h"

#include <cstdint>
#include <optional>

namespace srtc
{

class RtpExtension
{
public:
	static constexpr auto kOneByte = 0xBEDE;
	static constexpr auto kTwoByte = 0x1000;

    // A recognized "defined by profile" id, per RFC 5285: the one-byte format's fixed
    // id, or the two-byte format's id with any of the low 4 "app bits" set.
    [[nodiscard]] static bool isValidExtensionId(uint16_t id);

    RtpExtension();
    RtpExtension(uint16_t id, ByteBuffer&& data);

    RtpExtension(RtpExtension&& source) noexcept;
    RtpExtension& operator=(RtpExtension&& source) noexcept;

    void clear();

    [[nodiscard]] bool empty() const;
    [[nodiscard]] size_t size() const;

    [[nodiscard]] uint16_t getId() const;
    [[nodiscard]] const ByteBuffer& getData() const;

    [[nodiscard]] std::optional<uint16_t> findU16(uint8_t id) const;
    [[nodiscard]] std::optional<uint32_t> findU32(uint8_t id) const;
    [[nodiscard]] std::optional<uint64_t> findU64(uint8_t id) const;

    struct Value
    {
        const uint8_t* ptr;
        size_t size;
    };

    [[nodiscard]] std::optional<Value> findAny(uint8_t id) const;

    // Strips the trailing zero padding that was added to align the wire data to 4 bytes
    void trimPadding();

    [[nodiscard]] RtpExtension copy() const;

private:
    struct Element
    {
        uint8_t id;
        const uint8_t* ptr;
        size_t len;
    };

    // Walks to the next element, one-byte or two-byte format depending on mId.
    // Returns false once padding, a reserved id, or truncated data is reached, or if
    // mId is neither the one-byte indicator nor a recognized two-byte indicator.
    [[nodiscard]] bool nextElement(ByteReader& reader, Element& out) const;

    uint16_t mId;
    ByteBuffer mData;
};

} // namespace srtc

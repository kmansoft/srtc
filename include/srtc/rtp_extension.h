#pragma once

#include "srtc/byte_buffer.h"
#include <cstdint>

namespace srtc
{

class RtpExtension
{
public:
	static constexpr auto kOneByte = 0xBEDE;
	static constexpr auto kTwoByte = 0x1000;

    RtpExtension();
    RtpExtension(uint16_t id, ByteBuffer&& data);

    RtpExtension(RtpExtension&& source) noexcept;
    RtpExtension& operator=(RtpExtension&& source) noexcept;

    void clear();

    [[nodiscard]] bool empty() const;
    [[nodiscard]] size_t size() const;

    [[nodiscard]] uint16_t getId() const;
    [[nodiscard]] const ByteBuffer& getData() const;

    [[nodiscard]] RtpExtension copy() const;

	static ByteBuffer convertOneToTwoByte(const ByteBuffer& src);

private:
    uint16_t mId;
    ByteBuffer mData;
};

} // namespace srtc

#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/srtc.h"

#include <cstdint>
#include <optional>
#include <string>
#include <chrono>

namespace srtc
{

std::string bin_to_hex(const uint8_t* buf, size_t size);

ByteBuffer hex_to_bin(const std::string& hex);

bool operator==(const struct sockaddr_in& sin1, const struct sockaddr_in& sin2);
bool operator==(const struct sockaddr_in6& sin1, const struct sockaddr_in6& sin2);
bool operator==(const anyaddr& addr1, const anyaddr& addr2);

struct NtpTime {
    uint32_t seconds;   // Seconds since Jan 1, 1900
	uint32_t fraction; // Fraction of second (in 2^-32 seconds)
};

void getNtpTime(NtpTime& ntp);

int64_t getNtpUnixMicroseconds(const NtpTime& ntp);

int64_t getStableTimeMicros();

size_t compressNackList(const std::vector<uint16_t>& nackList, uint16_t* buf_seq, uint16_t* buf_blp);

template <class T>
class Filter
{
public:
	explicit Filter(float factor);

	void update(T value);
	void update(T value, const std::chrono::steady_clock::time_point& now);
	[[nodiscard]] T value() const;
	[[nodiscard]] std::chrono::steady_clock::time_point getWhenUpdated() const;

private:
	const float mFactor;
	std::optional<T> mValue;
	std::chrono::steady_clock::time_point mWhenUpdated;
};

} // namespace srtc

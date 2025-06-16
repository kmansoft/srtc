#include "srtc/util.h"

#include <cstring>
#include <ctime>

namespace srtc
{

std::string bin_to_hex(const uint8_t* buf, size_t size)
{
	static const char* const ALPHABET = "0123456789abcdef";

	std::string hex;
	for (size_t i = 0; i < size; i += 1) {
		hex += (ALPHABET[(buf[i] >> 4) & 0x0F]);
		hex += (ALPHABET[(buf[i]) & 0x0F]);
		if (i != size - 1) {
			hex += ':';
		}
	}
	return hex;
}

ByteBuffer hex_to_bin(const std::string& hex)
{
	ByteBuffer buf;
	ByteWriter writer(buf);

	size_t accumCount = 0;
	uint8_t accumBuf = 0;

	for (const auto ch : hex) {
		int accumNibble = -1;

		if (ch >= '0' && ch <= '9') {
			accumNibble = ch - '0';
		} else if (ch >= 'a' && ch <= 'f') {
			accumNibble = 10 + ch - 'a';
		} else if (ch >= 'A' && ch <= 'F') {
			accumNibble = 10 + ch - 'A';
		}

		if (accumNibble >= 0) {
			accumCount += 4;
			accumBuf = (accumBuf << 4) | accumNibble;

			if (accumCount == 8) {
				writer.writeU8(accumBuf);

				accumCount = 0;
				accumBuf = 0;
			}
		}
	}

	return buf;
}

bool operator==(const struct sockaddr_in& sin1, const struct sockaddr_in& sin2)
{
	return sin1.sin_family == sin2.sin_family && sin1.sin_port == sin2.sin_port &&
		   sin1.sin_addr.s_addr == sin2.sin_addr.s_addr;
}

bool operator==(const struct sockaddr_in6& sin1, const struct sockaddr_in6& sin2)
{
	return sin1.sin6_family == sin2.sin6_family && sin1.sin6_port == sin2.sin6_port &&
		   std::memcmp(&sin1.sin6_addr, &sin2.sin6_addr, sizeof(sin1.sin6_addr)) == 0;
}

bool operator==(const anyaddr& addr1, const anyaddr& addr2)
{
	return addr1.ss.ss_family == addr2.ss.ss_family &&
		   ((addr1.ss.ss_family == AF_INET && addr1.sin_ipv4 == addr2.sin_ipv4) ||
			(addr1.ss.ss_family == AF_INET6 && addr1.sin_ipv6 == addr2.sin_ipv6));
}

void getNtpTime(NtpTime& ntp)
{
#ifdef _WIN32
	FILETIME ft;
	ULARGE_INTEGER uli;
	uint64_t total_100ns;
	uint64_t ntp_seconds;
	uint64_t remaining_100ns;

	// Get current system time as FILETIME
	GetSystemTimeAsFileTime(&ft);

	// Convert FILETIME to 64-bit integer
	uli.LowPart = ft.dwLowDateTime;
	uli.HighPart = ft.dwHighDateTime;
	total_100ns = uli.QuadPart;

	// Convert to NTP epoch (Jan 1, 1900)
	// Difference between Windows epoch (1601) and NTP epoch (1900) = 301 years
	// 301 years * 365.2425 days/year * 24 hours/day * 3600 seconds/hour * 10^7 (100ns units)
	total_100ns += 94354848000000000ULL;

	// Extract seconds
	ntp_seconds = total_100ns / 10000000ULL;
	ntp.seconds = (uint32_t)ntp_seconds;

	// Extract fractional part
	remaining_100ns = total_100ns % 10000000ULL;
	// Convert to NTP fraction format (2^32 units per second)
	// fraction = (remaining_100ns * 2^32) / 10^7
	ntp.fraction = (uint32_t)((remaining_100ns * 4294967296ULL) / 10000000ULL);
#else
	// Get current time
	struct timespec current_time = {};
	clock_gettime(CLOCK_REALTIME, &current_time);

	// Convert Unix time to NTP time
	// NTP epoch starts at Jan 1, 1900
	// Unix epoch starts at Jan 1, 1970
	// Difference is 70 years plus 17 leap days = 2208988800 seconds
	constexpr uint32_t NTP_UNIX_OFFSET = 2208988800L;

	// Set the seconds field
	ntp.seconds = current_time.tv_sec + NTP_UNIX_OFFSET;

	// Convert nanoseconds to NTP fraction format (2^-32 seconds)
	// 2^32 / 10^9 = 4.294967296
	ntp.fraction = static_cast<uint32_t>(static_cast<double>(current_time.tv_nsec) * 4.294967296);
#endif
}

int64_t getSystemTimeMicros()
{
	// Get current time
#ifdef _WIN32
	const auto sinceEpoch = std::chrono::steady_clock::now().time_since_epoch();
	const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(sinceEpoch);
	return micros.count();
#else
	struct timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<int64_t>(ts.tv_sec) * 1000000l + ts.tv_nsec / 1000l;
#endif
}

template <class T>
Filter<T>::Filter(float factor)
	: mFactor(factor)
	, mValue(std::nullopt)
	, mTimestamp(0)
{
}

template <class T>
void Filter<T>::update(T value, int64_t timestamp)
{
	if (mValue.has_value()) {
		mValue = static_cast<T>(mValue.value() * (1 - mFactor) + value * mFactor);
	} else {
		mValue = value;
	}
	mTimestamp = timestamp;
}

template <class T>
T Filter<T>::value() const
{
	if (mValue.has_value()) {
		return mValue.value();
	}
	return {};
}

template <class T>
int64_t Filter<T>::getTimestamp() const
{
	return mTimestamp;
}

template class Filter<float>;

} // namespace srtc

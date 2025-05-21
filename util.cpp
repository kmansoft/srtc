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
	// Get current time
	struct timespec current_time = {};
	clock_gettime(CLOCK_REALTIME, &current_time);

	// Convert Unix time to NTP time
	// NTP epoch starts at Jan 1, 1900
	// Unix epoch starts at Jan 1, 1970
	// Difference is 70 years plus 17 leap days = 2208988800 seconds
	constexpr uint32_t NTP_UNIX_OFFSET = 2208988800UL;

	// Set the seconds field
	ntp.seconds = current_time.tv_sec + NTP_UNIX_OFFSET;

	// Convert nanoseconds to NTP fraction format (2^-32 seconds)
	// 2^32 / 10^9 = 4.294967296
	ntp.fraction = static_cast<uint32_t>(static_cast<double>(current_time.tv_nsec) * 4.294967296);
}


int64_t getSystemTimeMicros()
{
	// Get current time
	struct timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<int64_t>(ts.tv_sec) * 1000000l + ts.tv_nsec / 1000l;
}

} // namespace srtc

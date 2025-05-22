#pragma once

#include "srtc/byte_buffer.h"
#include "srtc/srtc.h"

#include <cstdint>
#include <string>
#include <optional>

namespace srtc
{

std::string bin_to_hex(const uint8_t* buf, size_t size);

ByteBuffer hex_to_bin(const std::string& hex);

bool operator==(const struct sockaddr_in& sin1, const struct sockaddr_in& sin2);
bool operator==(const struct sockaddr_in6& sin1, const struct sockaddr_in6& sin2);
bool operator==(const anyaddr& addr1, const anyaddr& addr2);

struct NtpTime {
    uint32_t seconds;  // Seconds since Jan 1, 1900
    uint32_t fraction; // Fraction of second (in 2^-32 seconds)
};

void getNtpTime(NtpTime& ntp);

template <typename T>
class TempBuffer {
public:
	TempBuffer();
	~TempBuffer();

	TempBuffer(const TempBuffer<T>& other) = delete;
	TempBuffer<T>& operator=(const TempBuffer<T>& other) = delete;

	T* ensure(size_t count);

private:
	T* mPtr;
	size_t mSize;
};

template <typename T>
TempBuffer<T>::TempBuffer()
	: mPtr(nullptr)
	, mSize(0)
{
}

template <typename T>
TempBuffer<T>::~TempBuffer() {
	delete[] mPtr;
	mPtr = nullptr;
	mSize = 0;
}

template <typename T>
T* TempBuffer<T>::ensure(size_t count) {
	if (mSize < count) {
		delete[] mPtr;
		mPtr = new T[count];
		mSize = count;
	}
	return mPtr;
}

int64_t getSystemTimeMicros();

template <class T>
class Filter
{
public:
	Filter();

	void update(T value);
	T get() const;

private:
	std::optional<T> mValue;
};

} // namespace srtc

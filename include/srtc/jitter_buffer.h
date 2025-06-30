#pragma once

#include <optional>
#include <cstdint>

namespace srtc {

// Extends sequential values to 64 bits on rollover, used for SEQ and RTP timestamps

template <typename T>
class ExtendedValue
{
public:
	ExtendedValue();

	std::optional<uint64_t> extend(T src);

private:
	uint64_t mRollover;
	std::optional<T> mLast;
};

}
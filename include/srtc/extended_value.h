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

	uint64_t extend(T src);
	[[nodiscard]] std::optional<uint64_t> get() const;

private:
    const uint64_t mIncrement;
	uint64_t mRollover;
	std::optional<T> mLast;
};

}
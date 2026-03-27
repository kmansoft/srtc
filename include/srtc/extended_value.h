#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace srtc
{

// Extends sequential values to 64 bits on rollover, used for RTP sequence numbers and timestamps

template <typename T>
class ExtendedValue
{
public:
    ExtendedValue();

    uint64_t extend(T src);
    [[nodiscard]] std::optional<uint64_t> get() const;

private:
    const uint64_t mRolloverIncrement;
    uint64_t mRolloverValue;
    std::chrono::steady_clock::time_point mRolloverTime;
    std::optional<T> mLastValue;
};

} // namespace srtc
#include "srtc/extended_value.h"

#include <limits>

namespace srtc
{

// Extended Value

template <typename T>
ExtendedValue<T>::ExtendedValue()
    : mIncrement(static_cast<uint64_t>(std::numeric_limits<T>::max()) + 1)
    , mRollover(mIncrement)
{
}

template <typename T>
uint64_t ExtendedValue<T>::extend(T src)
{
    if (!mLast.has_value()) {
        mLast = src;
        return mRollover | src;
    }

    constexpr auto margin = std::numeric_limits<T>::max() / 10;
    constexpr auto max = std::numeric_limits<T>::max();

    if (mLast.value() >= max - margin && src <= margin) {
        // Rollover
        mRollover += mIncrement;
        mLast = src;
        return mRollover | src;
    } else if (mLast.value() <= margin && src >= max - margin) {
        // We just had a rollover, and the new value wants to go backwards
        return (mRollover - mIncrement) | src;
    } else {
        mLast = src;
        return mRollover | src;
    }
}

template <typename T>
std::optional<uint64_t> ExtendedValue<T>::get() const
{
    if (mLast.has_value()) {
        return mRollover | mLast.value();
    }
    return std::nullopt;
}

template class ExtendedValue<uint16_t>;
template class ExtendedValue<uint32_t>;

} // namespace srtc
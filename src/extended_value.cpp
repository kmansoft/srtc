#include "srtc/extended_value.h"

#include <limits>

namespace
{

constexpr auto mRolloverBack = std::chrono::milliseconds(1000);

}

namespace srtc
{

// Extended Value

template <typename T>
ExtendedValue<T>::ExtendedValue()
    : mRolloverIncrement(static_cast<uint64_t>(std::numeric_limits<T>::max()) + 1)
    , mRolloverValue(0)
{
}

template <typename T>
uint64_t ExtendedValue<T>::extend(T src)
{
    // Lazy init
    if (mRolloverValue == 0) {
        mRolloverValue = mRolloverIncrement;
        mRolloverTime = std::chrono::steady_clock::now();
    }

    // Do we even have a previous value?
    if (!mLastValue.has_value()) {
        mLastValue = src;
        return mRolloverValue | src;
    }

    // Decide what we're going to do
    constexpr auto margin = std::numeric_limits<T>::max() / 4;
    constexpr auto max = std::numeric_limits<T>::max();

    if (mLastValue.value() >= max - margin && src <= margin) {
        // Rollover
        mRolloverValue += mRolloverIncrement;
        mRolloverTime = std::chrono::steady_clock::now();
        mLastValue = src;
        return mRolloverValue | src;
    }

    if (mLastValue.value() <= margin && src >= max - margin &&
        std::chrono::steady_clock::now() - mRolloverTime <= mRolloverBack) {
        // We just had a rollover, and the new value wants to go backwards
        return (mRolloverValue - mRolloverIncrement) | src;
    }

    mLastValue = src;
    return mRolloverValue | src;
}

template <typename T>
std::optional<uint64_t> ExtendedValue<T>::get() const
{
    if (mLastValue.has_value()) {
        return mRolloverValue | mLastValue.value();
    }
    return std::nullopt;
}

template class ExtendedValue<uint16_t>;
template class ExtendedValue<uint32_t>;

} // namespace srtc
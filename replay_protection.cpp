#include "srtc/replay_protection.h"

#include <cassert>
#include <cstring>

#include <limits>

namespace
{

uint32_t getRolloverForwadDistance(uint32_t maxPossibleValue, uint32_t curMaxValue, uint32_t value)
{
    assert(curMaxValue > value);
    return maxPossibleValue - curMaxValue + 1 + value;
}

bool isSet(const uint8_t* storage, uint32_t storageSize, uint32_t value)
{
    const auto index = (value / 8) % storageSize;
    const auto shift = value & (8 - 1);

    return (storage[index] & (1 << shift)) != 0;
}

void setImpl(uint8_t* storage, uint32_t storageSize, uint32_t value)
{
    const auto index = (value / 8) % storageSize;
    const auto shift = value & (8 - 1);

    storage[index] |= (1 << shift);
}

void clearImpl(uint8_t* storage, uint32_t storageSize, uint32_t value)
{
    const auto index = (value / 8) % storageSize;
    const auto shift = value & (8 - 1);

    storage[index] &= ~(1 << shift);
}

} // namespace

namespace srtc
{

ReplayProtection::ReplayProtection(uint32_t maxPossibleValue, uint32_t size)
    : mMaxPossibleValue(maxPossibleValue)
    , mSize(size)
    , mStorageSize((size + 8 - 1) / 8)
    , mMaxDistanceForward(size / 4)
    , mCurMax(0) // not used until we allocate mStorage
	, mStorage(nullptr)
{
    assert(maxPossibleValue == std::numeric_limits<uint16_t>::max() ||
           maxPossibleValue == std::numeric_limits<uint32_t>::max());
    assert(size <= 4096);
}

ReplayProtection::~ReplayProtection()
{
    delete[] mStorage;
}

bool ReplayProtection::canProceed(uint32_t value)
{
    if (!mStorage) {
        return true;
    }

    if (value == mCurMax) {
        return false;
    } else if (value > mCurMax) {
        const auto distance = value - mCurMax;
        if (distance > mMaxDistanceForward) {
            return false;
        }
        return true;
    } else if (getRolloverForwadDistance(mMaxPossibleValue, mCurMax, value) <= mMaxDistanceForward) {
        return true;
    } else {
        const auto distance = mCurMax - value;
        if (distance >= mSize) {
            return false;
        }

        return !isSet(mStorage, mStorageSize, value);
    }
}

bool ReplayProtection::set(uint32_t value)
{
    assert(canProceed(value));

    if (!mStorage) {
        mStorage = new uint8_t[mStorageSize];
        std::memset(mStorage, 0, mStorageSize);
        setImpl(mStorage, mStorageSize, value);
        mCurMax = value;
        return true;
    }

    if (value == mCurMax) {
        return false;
    } else if (value > mCurMax) {
        const auto distance = value - mCurMax;
        if (distance > mMaxDistanceForward) {
            return false;
        }

        setForward(value);
        return true;
    } else if (getRolloverForwadDistance(mMaxPossibleValue, mCurMax, value) <= mMaxDistanceForward) {
        setForward(value);
        return true;
    } else {
        const auto distance = mCurMax - value;
        if (distance >= mSize) {
            return false;
        }

        setImpl(mStorage, mStorageSize, value);
        return true;
    }
}

void ReplayProtection::setForward(uint32_t value)
{
    mCurMax = (mCurMax + 1) & mMaxPossibleValue;
    while (mCurMax != value) {
        clearImpl(mStorage, mStorageSize, mCurMax);
        mCurMax = (mCurMax + 1) & mMaxPossibleValue;
    }

    setImpl(mStorage, mStorageSize, mCurMax);
}

} // namespace srtc
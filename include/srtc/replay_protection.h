#pragma once

#include <cstdint>

namespace srtc {

class ReplayProtection final {
public:
    ReplayProtection(uint32_t maxPossibleValue,
                     uint32_t size);
    ~ReplayProtection();

    [[nodiscard]] bool canProceed(uint32_t value);
    [[nodiscard]] bool set(uint32_t value);

private:
    const uint32_t mMaxPossibleValue;
    const uint32_t mSize;
    const uint32_t mStorageSize;
    const uint32_t mMaxDistanceForward;

    uint32_t mCurMax;
    uint8_t* mStorage;

    void setForward(uint32_t value);
};

}

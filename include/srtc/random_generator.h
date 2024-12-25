#pragma once

#include <random>

namespace srtc {

template <class Value>
class RandomGenerator {
public:
    RandomGenerator(Value min, Value max);
    Value next();

private:
    std::random_device mRandomDevice;
    std::mt19937 mRandomTwister;
    std::uniform_int_distribution<Value> mRandomDist;
};

}

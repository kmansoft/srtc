#include "srtc/random_generator.h"

namespace srtc {

template <class Value>
RandomGenerator<Value>::RandomGenerator(Value min, Value max)
    : mRandomTwister(mRandomDevice())
    , mRandomDist(min, max)
{
}

template <class Value>
Value RandomGenerator<Value>::next()
{
    return mRandomDist(mRandomTwister);
}

template class RandomGenerator<int32_t>;
template class RandomGenerator<uint32_t>;

}

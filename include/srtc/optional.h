#pragma once

/*
 * Maybe we'll compile with C++ 14 which doesn't have std::optional.
 *
 * http://www.club.cc.cmu.edu/~ajo/disseminate/2017-02-15-Optional-From-Scratch.pdf
 */

#include <cassert>

namespace srtc {

struct nullopt_t { };

static constexpr nullopt_t nullopt;

template <class T>
class optional {
public:
    optional();
    optional(const T& value);
    optional(const nullopt_t& null);
    optional(const optional<T>& other);
    ~optional();

    optional<T>& operator=(const T& value);
    optional<T>& operator=(const nullopt_t& null);
    optional<T>& operator=(const optional<T>& other);

    [[nodiscard]] explicit operator bool() const;
    [[nodiscard]] bool has_value() const;

    [[nodiscard]] const T& value() const;
    [[nodiscard]] const T* operator->() const;

private:
    bool mHasValue;
    union {
        T mValue;
        char fill = { 0 };
    };
};

template <class T>
optional<T>::optional() : mHasValue(false) {
}

template <class T>
optional<T>::optional(const T& value)
    : mHasValue(true)
{
    new (&mValue) T(value);
}

template <class T>
optional<T>::optional([[maybe_unused]] const nullopt_t& null)
    : mHasValue(false)
{
}

template <class T>
optional<T>::optional(const optional<T>& other)
    : mHasValue(other.mHasValue)
{
    if (other.mHasValue) {
        new (&mValue) T(other.mValue);
    }
}

template <class T>
optional<T>::~optional()
{
    if (mHasValue) {
        mValue.~T();
    }
}

template <class T>
optional<T>& optional<T>::operator=(const T& value)
{
    if (mHasValue) {
        mValue = value;
    } else {
        new (&mValue) T(value);
        mHasValue = true;
    }

    return *this;
}

template <class T>
optional<T>& optional<T>::operator=([[maybe_unused]] const nullopt_t& null)
{
    if (mHasValue) {
        mValue.~T();
        mHasValue = false;
    }
}

template <class T>
optional<T>& optional<T>::operator=(const optional<T>& other)
{
    if (other.mHasValue) {
        this->operator=(other.mValue);
        return *this;
    }

    if (mHasValue) {
        mValue.~T();
        mHasValue = false;
    }
}

template <class T>
optional<T>::operator bool() const
{
    return mHasValue;
}

template <class T>
bool optional<T>::has_value() const
{
    return mHasValue;
}

template <class T>
const T& optional<T>::value() const
{
    assert(mHasValue);
    return mValue;
}

template <class T>
const T* optional<T>::operator->() const
{
    assert(mHasValue);
    return &mValue;
}


}

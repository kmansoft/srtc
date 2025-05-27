#pragma once

#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace srtc
{

// Allocates a fixed buffer once

template <typename T>
class FixedTempBuffer
{
public:
	FixedTempBuffer();
	~FixedTempBuffer();

	FixedTempBuffer(const FixedTempBuffer<T>& other) = delete;
	FixedTempBuffer<T>& operator=(const FixedTempBuffer<T>& other) = delete;

	[[nodiscard]] T* ensure(size_t count);

private:
	T* mPtr;
	size_t mSize;
};

template <typename T>
FixedTempBuffer<T>::FixedTempBuffer()
	: mPtr(nullptr)
	, mSize(0)
{
}

template <typename T>
FixedTempBuffer<T>::~FixedTempBuffer()
{
	delete[] mPtr;
	mPtr = nullptr;
	mSize = 0;
}

template <typename T>
T* FixedTempBuffer<T>::ensure(size_t count)
{
	if (mSize < count) {
		delete[] mPtr;
		mPtr = new T[count];
		mSize = count;
	}
	return mPtr;
}

// Append elements one at a time, like an std::vector

template <typename T>
class DynamicTempBuffer
{
public:
	DynamicTempBuffer();
	~DynamicTempBuffer();

	DynamicTempBuffer(const DynamicTempBuffer<T>& other) = delete;
	DynamicTempBuffer<T>& operator=(const DynamicTempBuffer<T>& other) = delete;

	void clear();
	[[nodiscard]] T* data() const;
	[[nodiscard]] size_t size() const;
	[[nodiscard]] T* append();

private:
	T* mPtr;
	size_t mPos;
	size_t mCap;
};

template <typename T>
DynamicTempBuffer<T>::DynamicTempBuffer()
	: mPtr(nullptr)
	, mPos(0)
	, mCap(0)
{
}

template <typename T>
DynamicTempBuffer<T>::~DynamicTempBuffer()
{
	delete[] mPtr;
	mPtr = nullptr;
}

template <typename T>
void DynamicTempBuffer<T>::clear()
{
	mPos = 0;
}

template <typename T>
T* DynamicTempBuffer<T>::data() const
{
	return mPtr;
}

template <typename T>
size_t DynamicTempBuffer<T>::size() const
{
	return mPos;
}

template <typename T>
T* DynamicTempBuffer<T>::append()
{
	if (mPos == mCap) {
		mCap = std::max(mPos + 16, mCap * 2);
		T* newPtr = new T[mCap];
		std::memcpy(newPtr, mPtr, mPos * sizeof(T));
		delete[] mPtr;
		mPtr = newPtr;
	}
	return mPtr + mPos++;
}

} // namespace srtc

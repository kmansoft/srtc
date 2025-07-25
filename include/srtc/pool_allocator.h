#pragma once

#include <memory>
#include <vector>

namespace srtc
{

class PoolAllocatorImpl
{
public:
    explicit PoolAllocatorImpl(size_t itemSize);
    ~PoolAllocatorImpl();

    [[nodiscard]] void* allocate();
    void release(void* ptr);

private:
    struct Item {
        union {
            size_t magic;
            Item* next;
        };
    };

    const size_t mItemSize;
    size_t mAllocatedCount;
    std::vector<void*> mPageList;
    Item* mFreeList;

    void addPage();
};

template <typename T>
class PoolAllocator
{
public:
    PoolAllocator();
    ~PoolAllocator() = default;

    [[nodiscard]] T* create();
    void destroy(T* ptr);

private:
    PoolAllocatorImpl mImpl;
};

template <typename T>
PoolAllocator<T>::PoolAllocator()
    : mImpl(sizeof(T))
{
}

template <typename T>
T* PoolAllocator<T>::create()
{
    const auto ptr = mImpl.allocate();
    return new(ptr) T;
}

template <typename T>
void PoolAllocator<T>::destroy(T* ptr)
{
    if (ptr) {
        ptr->~T();
        mImpl.release(ptr);
    }
}

} // namespace srtc

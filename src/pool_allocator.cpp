#include "srtc/pool_allocator.h"

#include <cassert>
#include <cstdlib>
#include <cstdio>

namespace
{

constexpr size_t kMagic = static_cast<size_t>(0x19720131CAFEBABEul);
constexpr size_t kPageItemCount = 64;

size_t round_item_size(size_t itemSize)
{
    while ((itemSize % sizeof(size_t)) != 0) {
        itemSize += 1;
    }
    return itemSize;
}

} // namespace

namespace srtc
{

PoolAllocatorImpl::PoolAllocatorImpl(size_t itemSize)
    : mItemSize(sizeof(Item) + round_item_size(itemSize))
    , mAllocatedCount(0)
    , mFreeList(nullptr)
{
}

PoolAllocatorImpl::~PoolAllocatorImpl()
{
    assert(mAllocatedCount == 0);

    for (const auto page : mPageList) {
        std::free(page);
    }
    mPageList.clear();
}

void* PoolAllocatorImpl::allocate()
{
    if (mFreeList == nullptr) {
        // We have no free items, add a bunch
        addPage();

        if (mFreeList == nullptr) {
            return nullptr;
        }
    }

    // Take the first item from the free list
    mAllocatedCount += 1;

    Item* item = mFreeList;
    mFreeList = mFreeList->next;

    item->magic = kMagic;
    return item + 1;
}

void PoolAllocatorImpl::release(void* ptr)
{
    if (ptr == nullptr) {
        return;
    }

    Item* item = static_cast<Item*>(ptr) - 1;
    assert(item->magic == kMagic);

    // Link into the free list
    assert(mAllocatedCount > 0);
    mAllocatedCount -= 1;

    item->next = mFreeList;
    mFreeList = item;
}

void PoolAllocatorImpl::addPage()
{
    // Allocate a new page
    const auto pageSize = kPageItemCount * mItemSize;
    const auto page = std::malloc(pageSize);
    if (page == nullptr) {
        return;
    }

    // Save it to be freed later
    mPageList.push_back(page);

    // Slice it into items and link them into the free list
    auto ptr = static_cast<uint8_t*>(page);
    for (size_t i = 0; i < kPageItemCount; i += 1, ptr += mItemSize) {
        const auto item = reinterpret_cast<Item*>(ptr);
        item->next = mFreeList;
        mFreeList = item;
    }
}

} // namespace srtc
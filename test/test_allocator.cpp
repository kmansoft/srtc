#include "srtc/pool_allocator.h"

#include <gtest/gtest.h>
#include <vector>
#include <cstring>

namespace
{

size_t gItemCout = 0;

struct Item {
    Item()
    {
        gItemCout += 1;
        std::memset(value, 0, sizeof(value));
    }

    ~Item()
    {
        gItemCout -= 1;
    }

    char value[13];
};
}

TEST(Allocator, Simple)
{
    srtc::PoolAllocator<Item> allocator;

    std::vector<Item*> itemList;

    // Allocate
    for (size_t i = 0; i < 201; i += 1) {
        const auto item = allocator.create();
        itemList.push_back(item);
    }

    ASSERT_EQ(itemList.size(), gItemCout);

    // Free every other item
    for (size_t i = 0; i < itemList.size(); i += 2) {
        allocator.destroy(itemList[i]);
    }
    for (size_t i = 1; i < itemList.size(); i += 2) {
        allocator.destroy(itemList[i]);
    }
    itemList.clear();

    ASSERT_EQ(itemList.size(), gItemCout);

    // Allocate some more
    for (size_t i = 0; i < 401; i += 1) {
        const auto item = allocator.create();
        itemList.push_back(item);
    }

    ASSERT_EQ(itemList.size(), gItemCout);

    // Free all
    for (size_t i = 0; i < itemList.size(); i += 1) {
        allocator.destroy(itemList[i]);
    }
    itemList.clear();

    ASSERT_EQ(itemList.size(), gItemCout);
}

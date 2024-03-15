#include <gtest/gtest.h>
#include "memory.hpp"

namespace {

  // simple MainMemory integration test - write & read the same memory
  TEST(MainMemoryTest, WriteAndRead) {
    MainMemory<8> mem;
    mem.writeBlock(0x8, Block<2>({0xDEADBEEFu, 0xBEEFCAFEu}));
    EXPECT_EQ(mem.readBlock<1>(0x8)[0], 0xDEADBEEFu);
    EXPECT_EQ(mem.readBlock<1>(0xC)[0], 0xBEEFCAFEu);
  }

  // direct-mapped, 4 words per block, 4 blocks in cache
  TEST(DirectMappedCacheTest, WriteAndRead) {
    Cache<4, 1, 4, 8> cache { MainMemory<8> {} };
    cache.writeBlock(0x0, Block<4>({0xAu, 0xBu, 0xCu, 0xDu}));
    EXPECT_EQ(cache.readBlock<1>(0x0)[0], 0xAu);
    EXPECT_EQ(cache.readBlock<1>(0x4)[0], 0xBu);
    EXPECT_EQ(cache.readBlock<1>(0x8)[0], 0xCu);
    EXPECT_EQ(cache.readBlock<1>(0xC)[0], 0xDu);
  }

  // 2-way associative, 4 words per block, 4 blocks in cache
  TEST(TwoWayCacheTest, WriteAndRead) {
    Cache<4, 2, 4, 8> cache { MainMemory<8> {} };
    cache.writeBlock(0x10, Block<1>({0xAu}));
    cache.writeBlock(0x20, Block<1>({0xBu}));
    cache.writeBlock(0x30, Block<1>({0xCu}));   // todo: test for eviction?
    EXPECT_EQ(cache.readBlock<1>(0x10)[0], 0xAu);
    EXPECT_EQ(cache.readBlock<1>(0x20)[0], 0xBu);
    EXPECT_EQ(cache.readBlock<1>(0x30)[0], 0xCu);
  }

}

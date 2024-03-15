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

  // use direct-mapped caches to avoid lru replacement issues
  TEST(WriteThroughCacheTest, AlwaysWritesThrough) {
    Cache<1, 1, 4, 8> cache { MainMemory<8> {} };
    cache.writeBlock(0x4, Block<1>({0xFACADEu}));   // 0x4 not in cache
    EXPECT_EQ(cache.mainMem.readBlock<1>(0x4)[0], 0xFACADEu);
    cache.readBlock<1>(0x4)[0];   // now, 0x4 is in cache
    cache.writeBlock(0x4, Block<1>({0xBEEFu}));     // still writes through
    EXPECT_EQ(cache.mainMem.readBlock<1>(0x4)[0], 0xBEEFu);
  }

  TEST(WriteBackCacheTest, NoWriteUnlessEviction) {
    Cache<1, 1, 4, 8, WriteScheme::WriteBack> cache { MainMemory<8> {} };
    cache.writeBlock(0x4, Block<1>({0xFACADEu})); // 0x4 not in cache
    EXPECT_EQ(cache.mainMem.readBlock<1>(0x4)[0], 0x0u);  // no eviction, no write
    EXPECT_EQ(cache.readBlock<1>(0x4)[0], 0xFACADEu);
    cache.writeBlock(0x4, Block<1>({0xBEEFu}));   // cache hit, no write
    EXPECT_EQ(cache.mainMem.readBlock<1>(0x4)[0], 0x0u);
    EXPECT_EQ(cache.readBlock<1>(0x4)[0], 0xBEEFu);
  }

  TEST(WriteBackCacheTest, WriteOnlyOnEviction) {
    // field: | tag(28) | index(2) | word(2) | 
    // 0x4 = |01|00, 0x14 = 1|01|00 -- causes conflict
    Cache<1, 1, 4, 8, WriteScheme::WriteBack> cache { MainMemory<8> {} };
    cache.writeBlock(0x4, Block<1>({0xFACADEu})); // 0x4 not in cache
    EXPECT_EQ(cache.mainMem.readBlock<1>(0x4)[0], 0x0u);  // no eviction, no write
    EXPECT_EQ(cache.readBlock<1>(0x4)[0], 0xFACADEu);
    cache.writeBlock(0x14, Block<1>({0xBEEFu}));   // eviction, write
    EXPECT_EQ(cache.mainMem.readBlock<1>(0x4)[0], 0xFACADEu);
    EXPECT_EQ(cache.readBlock<1>(0x14)[0], 0xBEEFu);
  }

  // todo: add some mechanism to get cache hit/miss info

}

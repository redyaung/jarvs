#include <gtest/gtest.h>
#include "memory.hpp"

namespace {

  // Simple MainMemory integration test - write & read the same memory
  TEST(MainMemoryTest, WriteAndRead) {
    TimedMainMemory mem(8, 1);
    mem._writeBlockTillDone(0x8, Block({0xDEADBEEFu, 0xBEEFCAFEu}));
    EXPECT_EQ(mem._readBlockTillDone(0x8, 1)[0], 0xDEADBEEFu);
    EXPECT_EQ(mem._readBlockTillDone(0xC, 1)[0], 0xBEEFCAFEu);
  }

  // Direct-mapped, 4 words per block, 4 blocks in cache
  TEST(DirectMappedCacheTest, WriteAndRead) {
    TimedCache cache(
      4, 1, 4, 
      WriteScheme::WriteThrough, ReplacementPolicy::Random,
      std::shared_ptr<TimedMemory>(new TimedMainMemory(8, 1)), 1
    );
    cache._writeBlockTillDone(0x0, Block({0xAu, 0xBu, 0xCu, 0xDu}));
    EXPECT_EQ(cache._readBlockTillDone(0x0, 1)[0], 0xAu);
    EXPECT_EQ(cache._readBlockTillDone(0x4, 1)[0], 0xBu);
    EXPECT_EQ(cache._readBlockTillDone(0x8, 1)[0], 0xCu);
    EXPECT_EQ(cache._readBlockTillDone(0xC, 1)[0], 0xDu);
  }

  // 2-way associative, 4 words per block, 4 blocks in cache
  TEST(TwoWayCacheTest, WriteAndRead) {
    TimedCache cache(
      4, 2, 4, 
      WriteScheme::WriteThrough, ReplacementPolicy::Random,
      std::shared_ptr<TimedMemory>(new TimedMainMemory(8, 1)), 1
    );
    cache._writeBlockTillDone(0x10, Block{0xAu});
    cache._writeBlockTillDone(0x20, Block{0xBu});
    cache._writeBlockTillDone(0x30, Block{0xCu});
    EXPECT_EQ(cache._readBlockTillDone(0x10, 1)[0], 0xAu);
    EXPECT_EQ(cache._readBlockTillDone(0x20, 1)[0], 0xBu);
    EXPECT_EQ(cache._readBlockTillDone(0x30, 1)[0], 0xCu);
  }

  // Use direct-mapped caches to avoid lru replacement issues
  TEST(WriteThroughCacheTest, AlwaysWritesThrough) {
    TimedCache cache(
      1, 1, 4, 
      WriteScheme::WriteThrough, ReplacementPolicy::Random,
      std::shared_ptr<TimedMemory>(new TimedMainMemory(8, 1)), 1
    );
    cache._writeBlockTillDone(0x4, Block{0xFACADEu});    // 0x4 not in cache
    EXPECT_EQ(cache.lowerMem->_readBlockTillDone(0x4, 1)[0], 0xFACADEu);

    cache._readBlockTillDone(0x4, 1)[0];         // now, 0x4 is in cache
    cache._writeBlockTillDone(0x4, Block{0xBEEFu});      // still writes through
    EXPECT_EQ(cache.lowerMem->_readBlockTillDone(0x4, 1)[0], 0xBEEFu);
  }

  TEST(WriteBackCacheTest, NoWriteUnlessEviction) {
    TimedCache cache(
      1, 1, 4, 
      WriteScheme::WriteBack, ReplacementPolicy::Random,
      std::shared_ptr<TimedMemory>(new TimedMainMemory(8, 1)), 1
    );
    cache._writeBlockTillDone(0x4, Block{0xFACADEu});        // 0x4 not in cache
    EXPECT_EQ(cache.lowerMem->_readBlockTillDone(0x4, 1)[0], 0x0u);  // no eviction, no write
    EXPECT_EQ(cache._readBlockTillDone(0x4, 1)[0], 0xFACADEu);

    cache._writeBlockTillDone(0x4, Block{0xBEEFu});          // cache hit, no write
    EXPECT_EQ(cache.lowerMem->_readBlockTillDone(0x4, 1)[0], 0x0u);
    EXPECT_EQ(cache._readBlockTillDone(0x4, 1)[0], 0xBEEFu);
  }

  TEST(WriteBackCacheTest, WriteOnlyOnEviction) {
    // field: | tag(28) | index(2) | word(2) | 
    // 0x4 = |01|00, 0x14 = 1|01|00 -- causes conflict
    TimedCache cache(
      1, 1, 4, 
      WriteScheme::WriteBack, ReplacementPolicy::Random,
      std::shared_ptr<TimedMemory>(new TimedMainMemory(8, 1)), 1
    );

    cache._writeBlockTillDone(0x4, Block{0xFACADEu});        // 0x4 not in cache
    EXPECT_EQ(cache.lowerMem->_readBlockTillDone(0x4, 1)[0], 0x0u);  // no eviction, no write
    EXPECT_EQ(cache._readBlockTillDone(0x4, 1)[0], 0xFACADEu);

    cache._writeBlockTillDone(0x14, Block{0xBEEFu});         // eviction, write
    EXPECT_EQ(cache.lowerMem->_readBlockTillDone(0x4, 1)[0], 0xFACADEu);
    EXPECT_EQ(cache._readBlockTillDone(0x14, 1)[0], 0xBEEFu);
  }
}

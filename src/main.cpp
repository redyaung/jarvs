#include <iostream>
#include "memory.hpp"

int main() {
  // Write-Back Write-Allocate
  MainMemory<8> mem;
  Cache<4, 4, 4, 8, WriteScheme::WriteBack, ReplacementPolicy::ApproximateLRU> cache{ mem };

  cache.writeBlock(0x10, Block<1>({0xDEADBEEFu}));
  std::cout << cache << std::endl;
  cache.writeBlock(0x14, Block<1>({0xFACADEu}));
  std::cout << cache << std::endl;

  Block w = cache.readBlock<1>(0x10);
  assert(uint32_t(w[0]) == 0xDEADBEEFu);
  std::cout << cache << std::endl;

  w = cache.readBlock<1>(0x14);
  assert(uint32_t(w[0]) == 0xFACADEu);
  std::cout << cache << std::endl;

  cache.writeBlock(0x30, Block<1>({0x3}));
  std::cout << cache << std::endl;
  cache.writeBlock(0x50, Block<1>({0x5}));
  std::cout << cache << std::endl;

  cache.writeBlock(0x70, Block<1>({0x7}));
  std::cout << cache << std::endl;
  cache.writeBlock(0x90, Block<1>({0x9}));
  std::cout << cache << std::endl;

  cache.writeBlock(0x30, Block<1>({0x3}));
  std::cout << cache << std::endl;
  cache.writeBlock(0x50, Block<1>({0x5}));
  std::cout << cache << std::endl;

  w = cache.readBlock<1>(0x10);
  assert(uint32_t(w[0]) == 0xDEADBEEFu);
  std::cout << cache << std::endl;

  w = cache.readBlock<1>(0x14);
  assert(uint32_t(w[0]) == 0xFACADEu);
  std::cout << cache << std::endl;

  // // 2-way set associative cache
  // MainMemory<8> mem;
  // Cache<4, 2, 4, 8> cache{ mem };

  // cache.writeBlock(0x10, Block<1>({0xDEADBEEFu}));
  // cache.writeBlock(0x30, Block<1>({0xFACADEu}));
  // cache.writeBlock(0x50, Block<1>({0xBEADu}));

  // Block w = cache.readBlock<1>(0x10);
  // assert(uint32_t(w[0]) == 0xDEADBEEFu);
  // std::cout << "0x10: " << w << std::endl;

  // w = cache.readBlock<1>(0x30);
  // assert(uint32_t(w[0]) == 0xFACADEu);
  // std::cout << "0x30: " << w << std::endl;

  // w = cache.readBlock<1>(0x50);
  // assert(uint32_t(w[0]) == 0xBEADu);
  // std::cout << "0x50: " << w << std::endl;

  // w = cache.readBlock<1>(0x10);
  // std::cout << "0x10: " << w << std::endl;

  // w = cache.readBlock<1>(0x30);
  // std::cout << "0x30: " << w << std::endl;


  // // Direct-mapped, 4 words per block, 4 blocks
  // MainMemory<8> mem;
  // Cache<4, 4, 8> cache{ mem };

  // cache.mainMem.writeBlock(0x0, Block<4>({0xAu, 0xBu, 0xCu, 0xDu}));

  // Block w = cache.readBlock<1>(0x0);
  // std::cout << "w: " << w << std::endl;
  // assert(uint32_t(w[0]) == 0xAu);

  // w = cache.readBlock<1>(0x4);
  // std::cout << "w: " << w << std::endl;
  // assert(uint32_t(w[0]) == 0xBu);

  // w = cache.readBlock<1>(0x8);
  // std::cout << "w: " << w << std::endl;
  // assert(uint32_t(w[0]) == 0xCu);

  // w = cache.readBlock<1>(0xC);
  // std::cout << "w: " << w << std::endl;
  // assert(uint32_t(w[0]) == 0xDu);


  // // Direct-mapped, 1 word per block, 4 words
  // MainMemory<8> mem;
  // Cache<1, 4, 8> cache{ mem };

  // cache.writeBlock(0x8, Block<1>({0xDEADBEEFu}));
  // cache.writeBlock(0x18, Block<1>({0xFACADEu}));

  // Block w = cache.readBlock<1>(0x8);
  // assert(uint32_t(w[0]) == 0xDEADBEEFu);
  // std::cout << "0x8: " << w << std::endl;

  // w = cache.readBlock<1>(0x18);
  // assert(uint32_t(w[0]) == 0xFACADEu);
  // std::cout << "0x18: " << w << std::endl;

  // w = cache.readBlock<1>(0x8);
  // std::cout << "0x8: " << w << std::endl;

  // w = cache.readBlock<1>(0x18);
  // std::cout << "0x18: " << w << std::endl;

  // // MainMemory Test
  // MainMemory<8> mem;
  // mem.writeBlock(0x8, Block<2>({0xDEADBEEF, 0xBEEFCADE}));
  // Block b = mem.readBlock<1>(0xC);
  // std::cout << "read block: " << b << std::endl;

  return 0;
}

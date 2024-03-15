#include <iostream>
#include "memory.hpp"

int main() {
  // Write-Back Write-Allocate
  Cache<4, 4, 4, 8, WriteScheme::WriteBack, ReplacementPolicy::ApproximateLRU> cache { 
    MainMemory<8> {}
  };

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

  return 0;
}

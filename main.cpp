#include <iostream>
#include <bitset>
#include <bit>
#include <stdexcept>
#include <algorithm>
#include <cassert>

bool isWordAligned(uint32_t addr) {
  return addr % 4 == 0;
}

// 32-bit word
struct Word {
  std::byte bytes[4];

  Word() = default;
  Word(uint32_t value) {
    auto b = std::bit_cast<std::byte*>(&value);
    // this probably only works for little endian
    for (auto i = 0; i < 4; i++) {
      bytes[i] = b[i];
    }
  }

  operator uint32_t() const {
    return std::bit_cast<uint32_t>(bytes);
  }
};

// main memory (N=8-bit address space = 2^8 = 256bytes)
template<uint8_t N>
struct MainMemory {
  std::byte bytes[1<< N];

  Word readWord(uint32_t addr) {
    std::cout << "reading from memory" << std::endl;
    if (!isWordAligned(addr)) {
      throw std::invalid_argument("address is not word-aligned");
    }
    Word word;
    std::copy(bytes + addr, bytes + addr + 4, word.bytes);
    return word;
  }

  void writeWord(uint32_t addr, Word word) {
    std::cout << "writing to memory" << std::endl;
    if (!isWordAligned(addr)) {
      throw std::invalid_argument("address is not word-aligned");
    }
    std::copy(word.bytes, word.bytes + 4, &bytes[addr]);
  }
};

// Goal for cache:
//  - four words per block -- block size = 4 * 32bits = 128bits (16bytes)
//  - four blocks in cache -- cache size = 4 * 128bits = 512bits (64bytes)

// simple write-through write-allocate cache
// one word per block; 4 blocks in cache -- 4 * 4bytes = 16bytes
// fields: |  tag(28)  | index(2) | block(0) | word(2) |
struct Cache {
  struct Entry {
    bool valid;
    uint32_t tag;
    Word block;
  };

  Entry entries[4];
  MainMemory<8> mainMem;

  Cache(MainMemory<8> mainMem) : mainMem{mainMem} {}

  Word readWord(uint32_t addr) {
    if (!isWordAligned(addr)) {
      throw std::invalid_argument("address is not word-aligned");
    }
    uint32_t tag = tagBits(addr);
    uint32_t index = indexBits(addr);
    if (!entries[index].valid || entries[index].tag != tag) { // cache miss
      std::cout << "cache miss" << std::endl;
      Word word = mainMem.readWord(addr);
      entries[index] = Entry{true, tag, word};
    } else {
      std::cout << "cache hit" << std::endl;
    }
    return entries[index].block;
  }

  void writeWord(uint32_t addr, Word word) {
    if (!isWordAligned(addr)) {
      throw std::invalid_argument("address is not word-aligned");
    }
    uint32_t tag = tagBits(addr);
    uint32_t index = indexBits(addr);
    if (entries[index].valid && entries[index].tag == tag) {  // cache hit
      std::cout << "cache hit - writing to cache entry" << std::endl;
      entries[index].block = word;
    }
    mainMem.writeWord(addr, word);
  }

  uint32_t tagBits(uint32_t addr) {
    return addr>> 4;
  }
  uint32_t indexBits(uint32_t addr) {
    return (addr>> 2) & 0b11;
  }
};

int main() {
  MainMemory<8> mem;
  Cache cache{ mem };

  cache.writeWord(0x8, 0xDEADBEEFu);
  cache.writeWord(0x18, 0xFACADEu);

  Word w = cache.readWord(0x8);
  assert(uint32_t(w) == 0xDEADBEEFu);
  std::cout << "0x8: " << std::hex << uint32_t(w) << std::endl;

  w = cache.readWord(0x18);
  assert(uint32_t(w) == 0xFACADEu);
  std::cout << "0x18: " << std::hex << uint32_t(w) << std::endl;

  cache.readWord(0x8);
  cache.readWord(0x18);

  return 0;
}

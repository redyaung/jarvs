#include <iostream>
#include <bitset>
#include <bit>
#include <stdexcept>
#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <optional>
#include <cstdlib>
#include <iomanip>
#include <string>
#include <thread>
#include <cstdio>

using namespace std::chrono_literals;

uint32_t __cache_delay = 1u;
uint32_t __memory_delay = 5u;

constexpr uint32_t nbytes(uint32_t nwords) {
  return nwords * 4;
}

// forward declarations
bool isAligned(uint32_t addr, uint32_t nwords);
uint32_t randInt(uint32_t min, uint32_t max);

// 32-bit word
struct Word {
  std::byte bytes[nbytes(1)];

  Word() = default;
  Word(uint32_t value) {
    auto b = std::bit_cast<std::byte*>(&value);
    // this probably only works for little endian
    for (auto i = 0; i < nbytes(1); i++) {
      bytes[i] = b[i];
    }
  }

  operator uint32_t() const {
    return std::bit_cast<uint32_t>(bytes);
  }
};

// a block containing w words
template<uint32_t W>
struct Block {
  std::byte bytes[nbytes(W)];

  Block() {
    std::fill(bytes, bytes + nbytes(W), std::byte{0u});
  }

  // better way: refer to make_tuple()
  Block(std::initializer_list<Word> words) {
    if (words.size() != W) {
      throw std::invalid_argument("number of words is not equal to W");
    }
    auto wordIdx = 0;
    for (auto it = words.begin(); it != words.end(); it++, wordIdx++) {
      for (auto byteIdx = 0; byteIdx < nbytes(1); byteIdx++) {
        bytes[nbytes(wordIdx) + byteIdx] = (*it).bytes[byteIdx];
      }
    }
  }

  Word operator[](int wordIdx) const {
    if (wordIdx < 0 || wordIdx >= W) {
      throw std::out_of_range("word index is out of bounds");
    }
    auto wordPtr = std::bit_cast<Word*>(&bytes[nbytes(wordIdx)]);
    return std::bit_cast<Word>(*wordPtr);
  }
};

// main memory (N=8-bit address space = 2^8 = 256bytes)
template<uint32_t N>
struct MainMemory {
  std::byte bytes[1<< N];

  // initialize memory with all zeros (unnecessary, but for demo purposes)
  MainMemory() {
    std::fill(bytes, bytes + (1<< N), std::byte{0u});
  }

  template<uint32_t W>
  Block<W> readBlock(uint32_t addr) {
    std::cout << "\t" << "reading from memory" << std::endl;
    if (!isAligned(addr, W)) {
      throw std::invalid_argument("address is not word-aligned");
    } else if (addr + nbytes(W) >= (1<< N)) {
      throw std::out_of_range("end address is out of address space");
    }
    for (auto cycle = 0; cycle < __memory_delay; cycle++) {
      std::cout << "...  " << std::flush;
      std::this_thread::sleep_for(500ms);
    }
    std::cout << std::endl;
    Block<W> block;
    std::copy(bytes + addr, bytes + addr + nbytes(W), block.bytes);
    return block;
  }

  template<uint32_t W>
  void writeBlock(uint32_t addr, Block<W> block) {
    std::cout << "\t" << "writing to memory" << std::endl;
    if (!isAligned(addr, W)) {
      throw std::invalid_argument("address is not word-aligned");
    } else if (addr + nbytes(W) >= (1<< N)) {
      throw std::out_of_range("end address is out of address space");
    }
    for (auto cycle = 0; cycle < __memory_delay; cycle++) {
      std::cout << "...  " << std::flush;
      std::this_thread::sleep_for(500ms);
    }
    std::cout << std::endl;
    std::copy(block.bytes, block.bytes + nbytes(W), bytes + addr);
  }
};

// Cache
// W: number of words in a block
// S: number of blocks in a set (set-associativity)
// B: number of blocks in the cache
// N: address space of the underlying main memory
// fields: | tag(rem) | index(log(B)) | block(log(W)) | word(2) |
template<uint32_t W, uint32_t S, uint32_t B, uint32_t N>
struct Cache {
  static_assert(std::has_single_bit(W), "W must be a power of 2");
  static_assert(std::has_single_bit(B), "B must be a power of 2");
  static_assert(std::has_single_bit(S), "S must be a power of 2");
  static_assert(B % S == 0, "set block count must divide total block count");

  static constexpr uint32_t nBlockBits = static_cast<uint32_t>(std::countr_zero(W));
  static constexpr uint32_t _indexBits = static_cast<uint32_t>(std::countr_zero(B));
  static constexpr uint32_t _setBits = static_cast<uint32_t>(std::countr_zero(S));
  static constexpr uint32_t nIndexBits = _indexBits - _setBits;

  struct Entry {
    bool valid;
    uint32_t tag;
    Block<W> block;
  };

  Entry entries[B];
  MainMemory<N> mainMem;

  Cache(MainMemory<N> mainMem) : mainMem{mainMem} {
    for (auto entryIdx = 0; entryIdx < B; entryIdx++) {
      entries[entryIdx].valid = false;
      entries[entryIdx].tag = 0u;
    }
  }

  template<uint32_t _W>
  Block<_W> readBlock(uint32_t addr) {
    static_assert(W % _W == 0, "requested block size must divide internal block size");
    if (!isAligned(addr, _W)) {
      throw std::invalid_argument("address is not word-aligned");
    }
    std::cout << "\t" << "reading from cache" << std::endl;
    for (auto cycle = 0; cycle < __cache_delay; cycle++) {
      std::cout << "...  " << std::flush;
      std::this_thread::sleep_for(500ms);
    }
    std::cout << std::endl;
    uint32_t tag = tagBits(addr);
    uint32_t setIdx = indexBits(addr);
    std::cout << "\t" << "index: " << setIdx << std::endl;
    auto entryIdx = findCacheEntry(addr);
    if (!entryIdx.has_value()) {  // cache miss
      std::cout << "\t" << "cache miss" << std::endl;
      // find a free cache entry -- may not exist
      uint32_t startingBlockIdx = setIdx * S;
      auto freeEntry = std::find_if(
        entries + startingBlockIdx,
        entries + startingBlockIdx + S,
        [](const Entry &entry) { return !entry.valid; }
      );
      // evict one of the entries randomly if there are no free entries
      if (freeEntry == entries + startingBlockIdx + S) {
        std::cout << "\t" << "performing cache eviction" << std::endl;
        uint32_t evictedIdx = randInt(startingBlockIdx, startingBlockIdx + S - 1);
        entries[evictedIdx].valid = false;
        entryIdx = std::make_optional(evictedIdx);
      } else {
        std::cout << "\t" << "found an empty entry in the set" << std::endl;
        entryIdx = std::make_optional(freeEntry - entries);
      }
      // read the block at the requested address from main memory
      uint32_t startingAddr = (addr / nbytes(W)) * nbytes(W);
      Block block = mainMem.template readBlock<W>(startingAddr);
      entries[*entryIdx] = {true, tag, block};
    } else {
      std::cout << "\t" << "cache hit" << std::endl;
    }
    // now, the entry at entryIdx contains a valid cache
    const Block<W> &entryBlock = entries[*entryIdx].block;
    Block<_W> requestedBlock;
    uint32_t blockOffset = addr % nbytes(W);
    std::copy(entryBlock.bytes + blockOffset, entryBlock.bytes + blockOffset + nbytes(_W),
      requestedBlock.bytes);
    return requestedBlock;
  }

  template<uint32_t _W>
  void writeBlock(uint32_t addr, Block<_W> block) {
    static_assert(W % _W == 0, "requested block size must divide internal block size");
    if (!isAligned(addr, _W)) {
      throw std::invalid_argument("address is not word-aligned");
    }
    std::cout << "\t" << "writing to cache" << std::endl;
    for (auto cycle = 0; cycle < __cache_delay; cycle++) {
      std::cout << "...  " << std::flush;
      std::this_thread::sleep_for(500ms);
    }
    std::cout << std::endl;
    if (auto entryIdx = findCacheEntry(addr)) {  // cache hit
      std::cout << "\t" << "cache hit - writing to cache entry" << std::endl;
      Block<W> &entryBlock = entries[*entryIdx].block;
      uint32_t blockAddr = addr % nbytes(W);
      std::copy(block.bytes, block.bytes + nbytes(_W), entryBlock.bytes + blockAddr);
    }
    mainMem.template writeBlock(addr, block);
  }

  std::optional<uint32_t> findCacheEntry(uint32_t addr) {
    uint32_t tag = tagBits(addr);
    uint32_t setIdx = indexBits(addr);
    uint32_t startingBlockIdx = setIdx * S;
    for (auto offset = 0; offset < S; offset++) {
      const Entry &entry = entries[startingBlockIdx + offset];
      if (entry.valid && entry.tag == tag) {
        return std::make_optional(startingBlockIdx + offset);
      }
    }
    return {};
  }

  uint32_t tagBits(uint32_t addr) {
    return addr>> (nIndexBits + nBlockBits + 2);
  }

  uint32_t indexBits(uint32_t addr) {
    return (addr>> (nBlockBits + 2)) & ((1<< nIndexBits) - 1);
  }
};

// Checks if address is aligned on an n-word boundary
bool isAligned(uint32_t addr, uint32_t nwords) {
  return addr % nbytes(nwords) == 0;
}

// better: consider using the new RNGs offered by C++'s <random>
uint32_t randInt(uint32_t min, uint32_t max) {
  return min + rand() % (max - min + 1);
}

std::ostream& operator<<(std::ostream& os, const Word& word) {
  os << std::hex << uint32_t(word);
  return os;
}

template<uint32_t W>
std::ostream& operator<<(std::ostream& os, const Block<W>& block) {
  os << "{";
  for (auto wordIdx = 0; wordIdx < W; wordIdx++) {
    os << block[wordIdx];
    if (wordIdx < W - 1) {
      os << ", ";
    }
  }
  os << "}";
  return os;
}

// cache pretty-printing
template<uint32_t W, uint32_t S, uint32_t B, uint32_t N>
std::ostream& operator<<(std::ostream& os, const Cache<W, S, B, N> &cache) {
  os << std::string(50, '-') << "\n";
  os << std::setw(8) << "status" << " | ";
  os << std::setw(8) << "tag" << " | ";
  os << "block" << "\n";
  for (auto entryIdx = 0; entryIdx < B; entryIdx++) {
    if (entryIdx % S == 0) {
      os << std::string(50, '-') << "\n";
    }
    const auto& entry = cache.entries[entryIdx];
    os << std::setw(8) << (entry.valid ? "valid" : "invalid") << " | ";
    os << std::setw(8) << std::hex << entry.tag << " | ";
    os << entry.block << "\n";
  }
  os << std::string(50, '-');
  return os;
}

template<uint32_t W, uint32_t S, uint32_t B, uint32_t N>
void interact(Cache<W, S, B, N> &cache) {
  while (true) {
    std::string userInput;
    std::getline(std::cin, userInput);
    if (userInput == "q") {
      break;
    }
    char commandCStr[64];
    uint32_t address, value;
    std::sscanf(userInput.data(), "%s %x %x", commandCStr, &address, &value);
    std::string command(commandCStr);
    if (command == "READ") {
      Block block = cache.template readBlock<1>(address);
      std::cout << "read word " << std::hex << block[0] << " from " << std::hex
        << address << std::endl;
    } else if (command == "WRITE") {
      cache.template writeBlock<1>(address, Block<1>({value}));
      std::cout << "wrote word " << std::hex << value << " to " << std::hex
        << address << std::endl;
    } else {
      continue;
    }
    std::cout << cache << std::endl;
  }
}

enum class SetAssociativity {
  DirectMapped, NAssociative, FullyAssociative
};

int main(int argc, char *argv[]) {
  std::cout << "8-bit address space; L1; 4 words per block; 4 blocks in cache" << std::endl;
  std::cout << "write-through no-write-allocate" << std::endl;

  SetAssociativity associativity = SetAssociativity::DirectMapped;
  if (argc > 1) {
    if (strcmp(argv[1], "-f") == 0) {
      associativity = SetAssociativity::FullyAssociative;
    } else if (strcmp(argv[1], "-s") == 0) {
      associativity = SetAssociativity::NAssociative;
    }
  }
  
  switch (associativity) {
    case SetAssociativity::DirectMapped: {
      std::cout << "direct-mapped" << std::endl;
      Cache<4, 1, 4, 8> cache{ MainMemory<8>() };
      interact(cache);
      break;
    }
    case SetAssociativity::FullyAssociative: {
      std::cout << "fully-associative" << std::endl;
      Cache<4, 4, 4, 8> cache{ MainMemory<8>() };
      interact(cache);
      break;
    }
    case SetAssociativity::NAssociative: {
      std::cout << "n-way associative" << std::endl;
      Cache<4, 2, 4, 8> cache{ MainMemory<8>() };
      interact(cache);
      break;
    }
  }

  // // Fully associatve cache
  // MainMemory<8> mem;
  // Cache<4, 2, 4, 8> cache{ mem };

  // cache.writeBlock(0x10, Block<1>({0xDEADBEEFu}));
  // cache.writeBlock(0x30, Block<1>({0xFACADEu}));
  // cache.writeBlock(0x50, Block<1>({0xBEADu}));
  // std::cout << cache << std::endl << std::flush;

  // Block w = cache.readBlock<1>(0x10);
  // assert(uint32_t(w[0]) == 0xDEADBEEFu);
  // std::cout << "0x10: " << w << std::endl;
  // std::cout << cache << std::endl << std::flush;

  // w = cache.readBlock<1>(0x30);
  // assert(uint32_t(w[0]) == 0xFACADEu);
  // std::cout << "0x30: " << w << std::endl;
  // std::cout << cache << std::endl << std::flush;

  // w = cache.readBlock<1>(0x50);
  // assert(uint32_t(w[0]) == 0xBEADu);
  // std::cout << "0x50: " << w << std::endl;
  // std::cout << cache << std::endl << std::flush;

  // w = cache.readBlock<1>(0x10);
  // std::cout << "0x10: " << w << std::endl << std::flush;

  // w = cache.readBlock<1>(0x30);
  // std::cout << "0x30: " << w << std::endl;
  // std::cout << cache << std::endl << std::flush;

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

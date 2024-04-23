#ifndef SIMULATOR_MEMORY_HPP
#define SIMULATOR_MEMORY_HPP

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
#include "utils.hpp"

// forward declarations
bool isAligned(uint32_t addr, uint32_t nwords);
uint32_t randInt(uint32_t min, uint32_t max);

// 32-bit word - make float conversions explicit as they are uncommon
struct Word {
  std::byte bytes[nbytes(1)];

  constexpr Word() {
    std::fill(bytes, bytes + nbytes(1), std::byte{0u});
  }
  constexpr Word(uint32_t value) : Word(from(value)) {}
  explicit constexpr Word(float value) : Word(from(value)) {}

  constexpr operator uint32_t() const { return to<uint32_t>(*this); }
  explicit constexpr operator float() const { return to<float>(*this); }

  template<typename T>
  static constexpr Word from(T value) {
    static_assert(sizeof(T) == 4);    // T must occupy 32 bytes
    Word word;
    auto b = std::bit_cast<std::byte*>(&value);
    // todo: currently only works for little endian
    for (auto i = 0; i < nbytes(1); i++) {
      word.bytes[i] = b[i];
    }
    return word;
  }

  template<typename T>
  static constexpr T to(Word word) {
    static_assert(sizeof(T) == 4);    // T must occupy 32 bytes
    return std::bit_cast<T>(word.bytes);
  }
};

// a block containing w words
template<uint32_t W>
struct Block {
  std::byte bytes[nbytes(W)];

  // initialize blocks with all zeros -- unnecessary but clarifying
  constexpr Block() {
    std::fill(bytes, bytes + nbytes(W), std::byte{0u});
  }

  // better way: refer to make_tuple()
  constexpr Block(std::initializer_list<Word> words) {
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

  constexpr Word operator[](int wordIdx) const {
    if (wordIdx < 0 || wordIdx >= W) {
      throw std::out_of_range("word index is out of bounds");
    }
    auto wordPtr = std::bit_cast<Word*>(&bytes[nbytes(wordIdx)]);
    return std::bit_cast<Word>(*wordPtr);
  }

  constexpr Word &operator[](int wordIdx) {
    if (wordIdx < 0 || wordIdx >= W) {
      throw std::out_of_range("word index is out of bounds");
    }
    auto wordPtr = std::bit_cast<Word*>(&bytes[nbytes(wordIdx)]);
    return *wordPtr;
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
    if (!isAligned(addr, W)) {
      throw std::invalid_argument("address " + std::to_string(addr) + " is not word-aligned");
    } else if (addr + nbytes(W) > (1<< N)) {
      throw std::out_of_range("end address is out of address space");
    }
    Block<W> block;
    std::copy(bytes + addr, bytes + addr + nbytes(W), block.bytes);
    return block;
  }

  template<uint32_t W>
  void writeBlock(uint32_t addr, Block<W> block) {
    if (!isAligned(addr, W)) {
      throw std::invalid_argument("address " + std::to_string(addr) + " is not word-aligned");
    } else if (addr + nbytes(W) > (1<< N)) {
      throw std::out_of_range("end address is out of address space");
    }
    std::copy(block.bytes, block.bytes + nbytes(W), bytes + addr);
  }
};

enum class WriteScheme {
  WriteThrough, WriteBack
};

enum class ReplacementPolicy {
  Random, PreciseLRU, ApproximateLRU
};

// Cache
// W: number of words in a block
// S: number of blocks in a set (set-associativity)
// B: number of blocks in the cache
// N: address space of the underlying main memory
// fields: | tag(rem) | index(log(B)) | block(log(W)) | word(2) |
template<uint32_t W, uint32_t S, uint32_t B, uint32_t N,
  WriteScheme WSch = WriteScheme::WriteThrough,
  ReplacementPolicy Plc = ReplacementPolicy::Random>
struct Cache {
  static_assert(std::has_single_bit(W), "W must be a power of 2");
  static_assert(std::has_single_bit(B), "B must be a power of 2");
  static_assert(std::has_single_bit(S), "S must be a power of 2");
  static_assert(B % S == 0, "set block count must divide total block count");

  static constexpr uint32_t nBlockBits = static_cast<uint32_t>(std::countr_zero(W));
  static constexpr uint32_t _indexBits = static_cast<uint32_t>(std::countr_zero(B));
  static constexpr uint32_t nSetBits = static_cast<uint32_t>(std::countr_zero(S));
  static constexpr uint32_t nIndexBits = _indexBits - nSetBits;
  static constexpr uint32_t setCount = B / S;

  struct Entry {
    bool valid = false;
    bool dirty = false;
    uint32_t tag = 0u;
    Block<W> block;
    int lastAccessedTime = 0;       // only used for precise LRU
  };

  Entry entries[B];
  MainMemory<N> mainMem;

  // used only for approximate LRU; generalization of the approximate LRU cache
  // scheme for 4-way set-associative caches (Patterson-Hennessy 5.8)
  std::bitset<S - 1> lruBits[setCount];
  int totalAccessCount = 0;

  // only the initial setting of valid bit is necessary
  Cache(MainMemory<N> mainMem) : mainMem{mainMem} {}

  template<uint32_t _W>
  Block<_W> readBlock(uint32_t addr) {
    static_assert(W % _W == 0, "requested block size must divide internal block size");
    if (!isAligned(addr, _W)) {
      throw std::invalid_argument("address " + std::to_string(addr) + " is not word-aligned");
    }
    uint32_t tag = tagBits(addr);
    uint32_t setIdx = indexBits(addr);
    std::optional<uint32_t> entryIdx = findCacheEntry(addr);
    if (!entryIdx.has_value()) {  // cache miss
      // find a free cache entry -- may not exist
      uint32_t startingEntryIdx = setIdx * S;
      auto freeEntry = std::find_if(
        entries + startingEntryIdx,
        entries + startingEntryIdx + S,
        [](const Entry &entry) { return !entry.valid; }
      );
      // evict one of the entries randomly if there are no free entries
      if (freeEntry == entries + startingEntryIdx + S) {
        auto selectEvictionIndex = [&]() -> uint32_t {
          if constexpr (Plc == ReplacementPolicy::PreciseLRU) {
            auto evictEntryIt = std::min_element(entries + startingEntryIdx,
              entries + startingEntryIdx + S,
              [](const Entry &e1, const Entry &e2) {
                return e1.lastAccessedTime < e2.lastAccessedTime;
              });
            return evictEntryIt - entries;
          } else if constexpr (Plc == ReplacementPolicy::ApproximateLRU) {
            uint32_t lruEntryIdx = 0;
            for (auto bit = 0, lruBit = 0; bit < nSetBits; ++bit) {
              bool choice = !lruBits[setIdx][lruBit];
              lruEntryIdx = (lruEntryIdx<< 1) + uint32_t(choice);
              lruBit = 2 * lruBit + 1 + int(choice);
            }
            return lruEntryIdx + startingEntryIdx;
          } else {
            return randInt(startingEntryIdx, startingEntryIdx + S - 1);
          }
        };
        uint32_t evictedIdx = selectEvictionIndex();
        // (write-back only) if cache entry is dirty, write it back before eviction
        if constexpr (WSch == WriteScheme::WriteBack) {
          if (entries[evictedIdx].dirty) {
            uint32_t evictedIndexBits = evictedIdx / S;
            uint32_t evictedTagBits = entries[evictedIdx].tag;
            uint32_t indexOffset = nBlockBits + 2;
            uint32_t tagOffset = indexOffset + nIndexBits;
            uint32_t evictedAddr = (evictedTagBits<< tagOffset) |
              (evictedIndexBits<< indexOffset);
            mainMem.template writeBlock<W>(evictedAddr, entries[evictedIdx].block);
          }
        }
        entryIdx = std::make_optional(evictedIdx);
      } else {
        entryIdx = std::make_optional(freeEntry - entries);
      }
      // read the block at the requested address from main memory
      uint32_t startingAddr = (addr / nbytes(W)) * nbytes(W);
      Block block = mainMem.template readBlock<W>(startingAddr);
      entries[*entryIdx] = Entry{true, false, tag, block};
    } else {

    }
    // now, the entry at entryIdx contains a valid cache
    Entry &entry = entries[*entryIdx];
    const Block<W> &entryBlock = entry.block;
    Block<_W> requestedBlock;
    uint32_t blockOffset = addr % nbytes(W);
    std::copy(entryBlock.bytes + blockOffset, entryBlock.bytes + blockOffset + nbytes(_W),
      requestedBlock.bytes);
    updateLRUInfo(entry, addr, *entryIdx % S);
    ++totalAccessCount;     // update analytics
    return requestedBlock;
  }

  template<uint32_t _W>
  void writeBlock(uint32_t addr, Block<_W> block) {
    static_assert(W % _W == 0, "requested block size must divide internal block size");
    if (!isAligned(addr, _W)) {
      throw std::invalid_argument("address " + std::to_string(addr) + " is not word-aligned");
    }
    if (std::optional<uint32_t> entryIdx = findCacheEntry(addr)) {  // cache hit
      Entry &entry = entries[*entryIdx];
      Block<W> &entryBlock = entry.block;
      uint32_t blockAddr = addr % nbytes(W);
      std::copy(block.bytes, block.bytes + nbytes(_W), entryBlock.bytes + blockAddr);
      entry.dirty = true;
      updateLRUInfo(entry, addr, *entryIdx % S);
    } else {  // cache miss
      // (write-back only) put the block containing address into cache, then write
      if constexpr (WSch == WriteScheme::WriteBack) {
        this->readBlock<_W>(addr);
        this->writeBlock<_W>(addr, block);
        return;
      }
    }
    // (write-through only) write unconditionally to the main memory
    if constexpr (WSch == WriteScheme::WriteThrough) {
      mainMem.template writeBlock(addr, block);
    }
    ++totalAccessCount;     // update analytics
  }

  std::optional<uint32_t> findCacheEntry(uint32_t addr) {
    uint32_t tag = tagBits(addr);
    uint32_t setIdx = indexBits(addr);
    uint32_t startingEntryIdx = setIdx * S;
    for (auto offset = 0; offset < S; offset++) {
      const Entry &entry = entries[startingEntryIdx + offset];
      if (entry.valid && entry.tag == tag) {
        return std::make_optional(startingEntryIdx + offset);
      }
    }
    return {};
  }

  // localBlockIdx refers to the index of the block or entry in the set
  void updateLRUInfo(Entry &entry, uint32_t addr, uint32_t localBlockIdx) {
    if constexpr (Plc == ReplacementPolicy::PreciseLRU) {
      entry.lastAccessedTime = totalAccessCount;
    } else if constexpr (Plc == ReplacementPolicy::ApproximateLRU) {
      uint32_t setIdx = indexBits(addr);
      std::bitset<nSetBits> blockAddr(localBlockIdx);
      // iterate from the most significant bit
      for (int bit = nSetBits - 1, lruBit = 0; bit >= 0; bit--) {
        lruBits[setIdx][lruBit] = blockAddr[bit];
        lruBit = 2 * lruBit + 1 + int(blockAddr[bit]);
      }
    }
  }

  uint32_t tagBits(uint32_t addr) {
    return addr>> (nIndexBits + nBlockBits + 2);
  }

  uint32_t indexBits(uint32_t addr) {
    return (addr>> (nBlockBits + 2)) & ((1<< nIndexBits) - 1);
  }
};

// Checks if address is aligned on an n-word boundary
inline bool isAligned(uint32_t addr, uint32_t nwords) {
  return addr % nbytes(nwords) == 0;
}

// better: consider using the new RNGs offered by C++'s <random>
inline uint32_t randInt(uint32_t min, uint32_t max) {
  return min + rand() % (max - min + 1);
}

inline std::ostream& operator<<(std::ostream& os, const Word& word) {
  os << uint32_t(word);
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
template<uint32_t W, uint32_t S, uint32_t B, uint32_t N,
  WriteScheme WSch, ReplacementPolicy Plc>
std::ostream& operator<<(std::ostream& os, const Cache<W, S, B, N, WSch, Plc> &cache) {
  os << std::string(60, '-') << "\n";
  os << std::setw(8) << "status" << " | ";
  os << std::setw(8) << "dirty" << " | ";
  os << std::setw(8) << "tag" << " | ";
  if constexpr (Plc == ReplacementPolicy::PreciseLRU) {
    os << std::setw(8) << "lru bits" << " | ";
  }
  os << "block" << "\n";
  os << std::string(60, '-') << "\n";
  for (auto entryIdx = 0; entryIdx < B; entryIdx++) {
    const auto& entry = cache.entries[entryIdx];
    os << std::setw(8) << (entry.valid ? "valid" : "invalid") << " | ";
    os << std::setw(8) << (entry.dirty ? "yes" : "no") << " | ";
    os << std::setw(8) << std::hex << entry.tag << std::dec << " | ";
    if constexpr (Plc == ReplacementPolicy::PreciseLRU) {
      os << std::setw(8) << std::dec << entry.lastAccessedTime << " | ";
    }
    os << entry.block << "\n";
    if (entryIdx % S == S - 1) {
      if constexpr (Plc == ReplacementPolicy::ApproximateLRU) {
        uint32_t setIdx = entryIdx / S;
        os << std::string(10, '=') << "\n";
        os << "lru bits (prints mru; reversed): " << cache.lruBits[setIdx] << "\n";
      }
      os << std::string(60, '-') << (entryIdx < B - 1 ? "\n" : "");
    }
  }
  return os;
}

#endif

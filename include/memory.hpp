#ifndef SIMULATOR_MEMORY_HPP
#define SIMULATOR_MEMORY_HPP

#include <iostream>
#include <bitset>
#include <bit>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <optional>
#include <cstdlib>
#include <iomanip>
#include <string>
#include <vector>
#include <format>

#include "utils.hpp"
#include "generator.hpp"

enum struct WriteScheme {
  WriteThrough, WriteBack
};

enum struct ReplacementPolicy {
  Random, PreciseLRU, ApproximateLRU
};

enum struct MemoryState {
  Ready, Reading, Writing
};

// Represents a 32-bit word that can currently hold either an integer or a float.
struct Word {
public:
  constexpr Word();
  constexpr Word(uint32_t value);
  explicit constexpr Word(float value);

  constexpr operator uint32_t() const;
  explicit constexpr operator float() const;

  template<typename T>
  static constexpr Word from(T value);

  template<typename T>
  static constexpr T to(Word word);

private:
  std::byte bytes[nbytes(1)];
};

// A block contains a fixed number of words which is determined at initialization time. Therefore,
// blocks can only be assigned to one another if their sizes agree. Internally, a block contains
// a contiguous sequence of words which can be accessed using the subscript operator.
struct Block {
public:
  Block(size_t nwords) : words(nwords)  {}
  Block(std::initializer_list<Word> words) : words(words) {}

  Block(const Block &other) = default;
  Block& operator=(const Block &other);

  Block(Block &&other) = default;
  Block& operator=(Block &&other);

  // Word Access
  size_t size() const;
  Word operator[](size_t wordIdx) const;
  Word &operator[](size_t wordIdx);

  // Sub-block access
  Block getSubblock(size_t from, size_t len) const;
  void setSubblock(size_t from, const Block &block);

private:
  std::vector<Word> words;
};

struct TimedMemory {
public:
  std::optional<Block> readBlock(uint32_t addr, size_t blockSize);
  bool writeBlock(uint32_t addr, const Block &block);
  MemoryState getState() const;

  // Instantaneous analogs for the timed readBlock and writeBlock functions.
  // Should only be used in *testing* code.
  Block _readBlockTillDone(uint32_t addr, size_t blockSize);
  void _writeBlockTillDone(uint32_t addr, const Block &block);

  virtual ~TimedMemory();

protected:
  MemoryState state = MemoryState::Ready;

  virtual Generator<std::optional<Block>> makeReadGen(uint32_t addr, size_t blockSize) = 0; 
  virtual Generator<bool> makeWriteGen(uint32_t addr, const Block &block) = 0;

private:
  std::optional<Generator<std::optional<Block>>> readGen;
  std::optional<Generator<bool>> writeGen;

  // Stores the current address that is being operated on (for invariant check)
  uint32_t curAddr;
};

struct TimedMainMemory : public TimedMemory {
public:
  const size_t addressSpace;
  const size_t latency;

  Block storage;

  TimedMainMemory(size_t addressSpace, size_t latency);

protected:
  Generator<std::optional<Block>> makeReadGen(uint32_t addr, size_t blockSize) override;
  Generator<bool> makeWriteGen(uint32_t addr, const Block &block) override;
};

struct TimedCache : public TimedMemory {
public:
  struct Entry {
    bool valid = false;
    bool dirty = false;
    uint32_t tag = 0u;
    Block block;
    int lastAccessedTime = 0;   // Used only for precise LRU

    Entry(size_t blockSize);
  };

  const size_t blockSize;       // Number of words in a block
  const size_t setSize;         // Number of blocks in a set (set associativity)
  const size_t cacheSize;       // Number of blocks in the cache
  const WriteScheme scheme;
  const ReplacementPolicy policy;
  const size_t latency;

  std::vector<Entry> entries;
  std::shared_ptr<TimedMemory> lowerMem;

  TimedCache(
    size_t blockSize,
    size_t setSize,
    size_t cacheSize,
    WriteScheme scheme,
    ReplacementPolicy policy,
    std::shared_ptr<TimedMemory> lowerMem,
    size_t latency
  );

protected:
  Generator<std::optional<Block>> makeReadGen(uint32_t addr, size_t readBlockSize) override;
  Generator<bool> makeWriteGen(uint32_t addr, const Block &block) override;

private:
  std::vector<std::vector<bool>> lruBits;   // Used only for approx LRU (Patterson-Hennessy 5.8)
  int totalAccessCount = 0;                 // Used only for precise LRU

  std::optional<uint32_t> findCacheEntry(uint32_t addr);

  // Updates internal data structures if the replacement policy is either approximate
  // or precise LRU.
  //  - localBlockIdx: index of the block or entry in the set
  void updateLRUInfo(Entry &entry, uint32_t addr, uint32_t localBlockIdx);

  // fields: | tag(rem) | index(log(B)) | block(log(W)) | word(2) |
  inline size_t blockBitCount() const;
  inline size_t setBitCount() const;
  inline size_t indexBitCount() const;
  inline size_t setCount() const;
  inline uint32_t tagBits(uint32_t addr) const;
  inline uint32_t indexBits(uint32_t addr) const;
};


// Word

constexpr Word::Word() {
  std::fill(bytes, bytes + nbytes(1), std::byte{0u});
}
constexpr Word::Word(uint32_t value) : Word(from(value)) {}
constexpr Word::Word(float value) : Word(from(value)) {}

constexpr Word::operator uint32_t() const { return to<uint32_t>(*this); }
constexpr Word::operator float() const { return to<float>(*this); }

template<typename T>
constexpr Word Word::from(T value) {
  static_assert(sizeof(T) == 4);
  Word word;
  auto b = std::bit_cast<std::byte*>(&value);
  // TODO: Generalize this to big-endian
  for (auto i = 0; i < nbytes(1); i++) {
    word.bytes[i] = b[i];
  }
  return word;
}

template<typename T>
constexpr T Word::to(Word word) {
  static_assert(sizeof(T) == 4);
  return std::bit_cast<T>(word.bytes);
}

#endif

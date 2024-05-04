#include "memory.hpp"


// Block

Block& Block::operator=(const Block &other) {
  assert (other.words.size() == words.size());
  words = other.words;
  return *this;
}

Block& Block::operator=(Block &&other) {
  assert (other.words.size() == words.size());
  words = std::move(other.words);
  return *this;
} 

size_t Block::size() const {
  return words.size();
}

Word Block::operator[](size_t wordIdx) const {
  return words.at(wordIdx);
}

Word &Block::operator[](size_t wordIdx) {
  return words.at(wordIdx);
}

Block Block::getSubblock(size_t from, size_t len) const {
  assert (from + len <= words.size());
  Block subblock(len);
  std::copy(words.begin() + from, words.begin() + from + len, subblock.words.begin());
  return subblock;
}

void Block::setSubblock(size_t from, const Block &block) {
  assert (from + block.size() <= words.size());
  std::copy(block.words.begin(), block.words.end(), words.begin() + from);
}


// TimedMemory

TimedMemory::~TimedMemory() {}

std::optional<Block> TimedMemory::readBlock(uint32_t addr, size_t blockSize) {
  assert (state != MemoryState::Writing);
  assert (state == MemoryState::Ready || curAddr == addr);
  if (state == MemoryState::Ready) {
    readGen = std::move(makeReadGen(addr, blockSize));
    state = MemoryState::Reading;
  }
  curAddr = addr;
  std::optional<Block> result = readGen.value()();
  if (result) {
    state = MemoryState::Ready;
  }
  return result;
}

bool TimedMemory::writeBlock(uint32_t addr, const Block &block) {
  assert (state != MemoryState::Reading);
  assert (state == MemoryState::Ready || curAddr == addr);
  if (state == MemoryState::Ready) {
    writeGen = std::move(makeWriteGen(addr, block));
    state = MemoryState::Writing;
  }
  curAddr = addr;
  bool result = writeGen.value()();
  if (result) {
    state = MemoryState::Ready;
  }
  return result;
}

MemoryState TimedMemory::getState() const {
  return state;
}

Block TimedMemory::_readBlockTillDone(uint32_t addr, size_t blockSize) {
  std::optional<Block> block = std::nullopt;
  while (!block) {
    block = readBlock(addr, blockSize);
  }
  return *block;
}

void TimedMemory::_writeBlockTillDone(uint32_t addr, const Block &block) {
  while (!writeBlock(addr, block)) {}
}


// TimedMainMemory

TimedMainMemory::TimedMainMemory(size_t addressSpace, size_t latency)
  : addressSpace{addressSpace},
    latency{latency},
    storage(1<< (addressSpace - 2)),
    TimedMemory{} {}

Generator<std::optional<Block>> TimedMainMemory::makeReadGen(uint32_t addr, size_t blockSize) {
  assert(isAligned(addr, blockSize));
  assert(addr + nbytes(blockSize) <= (1<< addressSpace));
  for (auto cycle = 1; cycle < latency; cycle++) {
    co_yield std::nullopt;
  }
  co_yield storage.getSubblock(addr>> 2, blockSize);
}

Generator<bool> TimedMainMemory::makeWriteGen(uint32_t addr, const Block &block) {
  assert(isAligned(addr, block.size()));
  assert(addr + nbytes(block.size()) <= (1<< addressSpace));
  for (auto cycle = 1; cycle < latency; cycle++) {
    co_yield false;
  }
  storage.setSubblock(addr>> 2, block);
  co_yield true;
}


// TimedCache

TimedCache::Entry::Entry(size_t blockSize) : block(blockSize) {}

TimedCache::TimedCache(
  size_t blockSize,
  size_t setSize,
  size_t cacheSize,
  WriteScheme scheme,
  ReplacementPolicy policy,
  std::shared_ptr<TimedMemory> lowerMem,
  size_t latency
) :
  blockSize{blockSize},
  setSize{setSize},
  cacheSize{cacheSize},
  scheme{scheme},
  policy{policy},
  latency{latency},
  entries(cacheSize, Entry(blockSize)),
  lowerMem{lowerMem},
  lruBits(setCount(), std::vector<bool>(setSize - 1, false)),
  TimedMemory{}
{
  // Block size, set size and cache size must all be powers of 2.
  assert(std::has_single_bit(blockSize));
  assert(std::has_single_bit(setSize));
  assert(std::has_single_bit(cacheSize));

  // Number of blocks in the cache must be a multiple of that in a set.
  assert(cacheSize % setSize == 0);
}

Generator<std::optional<Block>> TimedCache::makeReadGen(uint32_t addr, size_t readBlockSize) {
  assert (blockSize % readBlockSize == 0);
  assert (isAligned(addr, readBlockSize));
  for (auto cycle = 1; cycle < latency; cycle++) {
    co_yield std::nullopt;
  }

  uint32_t tag = tagBits(addr);
  uint32_t setIdx = indexBits(addr);
  std::optional<uint32_t> entryIdx = findCacheEntry(addr);

  bool isCacheHit = entryIdx.has_value();
  if (!isCacheHit) {
    // Find a free cache entry to store currently read block (which may not exist)
    uint32_t startingEntryIdx = setIdx * setSize;
    auto freeEntry = std::find_if(
      entries.begin() + startingEntryIdx,
      entries.begin() + startingEntryIdx + setSize,
      [](const Entry &entry) { return !entry.valid; }
    );

    // Compute the index of the cache entry (after some preliminary actions)
    bool noFreeEntries = (freeEntry == entries.begin() + startingEntryIdx + setSize);
    if (noFreeEntries) {
      auto selectEvictionIndex = [&]() -> uint32_t {
        if (policy == ReplacementPolicy::PreciseLRU) {
          auto evictEntryIt = std::min_element(
            entries.begin() + startingEntryIdx,
            entries.begin() + startingEntryIdx + setSize,
            [](const Entry &e1, const Entry &e2) {
              return e1.lastAccessedTime < e2.lastAccessedTime;
            });
          return evictEntryIt - entries.begin();
        } else if (policy == ReplacementPolicy::ApproximateLRU) {
          uint32_t lruEntryIdx = 0;
          for (int bit = 0, lruBit = 0; bit < setBitCount(); ++bit) {
            bool choice = !lruBits[setIdx][lruBit];
            lruEntryIdx = (lruEntryIdx<< 1) + uint32_t(choice);
            lruBit = 2 * lruBit + 1 + int(choice);
          }
          return lruEntryIdx + startingEntryIdx;
        } else {
          return randInt(startingEntryIdx, startingEntryIdx + setSize - 1);
        }
      };

      // Evict one of the entries randomly if there are no free entries
      uint32_t evictedIdx = selectEvictionIndex();

      // (Write-back only) If cache entry is dirty, write it back before eviction.
      if (scheme == WriteScheme::WriteBack && entries[evictedIdx].dirty) {
        uint32_t evictedIndexBits = evictedIdx / setSize;
        uint32_t evictedTagBits = entries[evictedIdx].tag;
        uint32_t indexOffset = blockBitCount() + 2;
        uint32_t tagOffset = indexOffset + indexBitCount();
        uint32_t evictedAddr = (evictedTagBits<< tagOffset) | (evictedIndexBits<< indexOffset);
        while (!lowerMem->writeBlock(evictedAddr, entries[evictedIdx].block)) {
          co_yield std::nullopt;
        }
      }
      entryIdx = std::make_optional(evictedIdx);
    } else {
      entryIdx = std::make_optional(freeEntry - entries.begin());
    }

    // Read the block from the underlying memory and store it in the cache
    uint32_t startingAddr = (addr / nbytes(blockSize)) * nbytes(blockSize);
    std::optional<Block> readResult;
    do {
      readResult = lowerMem->readBlock(startingAddr, blockSize);
      co_yield std::nullopt;
    } while (!readResult);
    Entry &entry = entries[*entryIdx];
    entry.valid = true; entry.dirty = false; entry.tag = tag; entry.block = *readResult;
  }

  Entry &entry = entries[*entryIdx];
  uint32_t blockOffset = addr % nbytes(blockSize);
  Block requestedBlock = entry.block.getSubblock(blockOffset>> 2, readBlockSize);
  updateLRUInfo(entry, addr, *entryIdx % setSize);
  co_yield requestedBlock;
}

Generator<bool> TimedCache::makeWriteGen(uint32_t addr, const Block &block) {
  assert (blockSize % block.size() == 0);
  assert (isAligned(addr, block.size()));
  for (auto cycle = 1; cycle < latency; cycle++) {
    co_yield false;
  }

  std::optional<uint32_t> entryIdx = findCacheEntry(addr);

  // (Write-back only) On cache miss, read the block into the cache first.
  if (scheme == WriteScheme::WriteBack && !entryIdx) {
    std::cerr << "write-back cache miss!" << std::endl;
    auto readGen = this->makeReadGen(addr, blockSize);
    while (!readGen()) {
      co_yield false;
    }
    entryIdx = findCacheEntry(addr);
    assert (entryIdx.has_value());
  }

  // Update the cache on cache hit
  if (entryIdx) {
    Entry &entry = entries[*entryIdx];
    uint32_t blockOffset = addr % nbytes(blockSize);
    std::cerr << "updating entry.block (" << (blockOffset>> 2) << ")" << std::endl;
    entry.block.setSubblock(blockOffset>> 2, block);
    entry.dirty = true;
    updateLRUInfo(entry, addr, *entryIdx % setSize);
  }

  // (Write-through only) Always write to the underlying memory
  if (scheme == WriteScheme::WriteThrough) {
    while (!lowerMem->writeBlock(addr, block)) {
      co_yield false;
    }
  }

  co_yield true;
}

std::optional<uint32_t> TimedCache::findCacheEntry(uint32_t addr) {
  uint32_t tag = tagBits(addr);
  uint32_t setIdx = indexBits(addr);
  uint32_t startingEntryIdx = setIdx * setSize;
  for (auto offset = 0; offset < setSize; offset++) {
    const Entry &entry = entries[startingEntryIdx + offset];
    if (entry.valid && entry.tag == tag) {
      return std::make_optional(startingEntryIdx + offset);
    }
  }
  return std::nullopt;
}

void TimedCache::updateLRUInfo(Entry &entry, uint32_t addr, uint32_t localBlockIdx) {
  if (policy == ReplacementPolicy::PreciseLRU) {
    entry.lastAccessedTime = totalAccessCount++;
  } else if (policy == ReplacementPolicy::ApproximateLRU) {
    uint32_t setIdx = indexBits(addr);
    std::bitset<32> blockAddr(localBlockIdx);
    // iterate from the most significant bit
    for (int bit = setBitCount() - 1, lruBit = 0; bit >= 0; bit--) {
      lruBits[setIdx][lruBit] = blockAddr[bit];
      lruBit = 2 * lruBit + 1 + int(blockAddr[bit]);
    }
  }
}

size_t TimedCache::blockBitCount() const {
  return static_cast<size_t>(std::countr_zero(blockSize));
}

size_t TimedCache::setBitCount() const {
  return static_cast<size_t>(std::countr_zero(setSize));
}

size_t TimedCache::indexBitCount() const {
  return static_cast<size_t>(std::countr_zero(cacheSize)) - setBitCount();
}

size_t TimedCache::setCount() const {
  return cacheSize / setSize;
}

uint32_t TimedCache::tagBits(uint32_t addr) const {
  return addr>> (indexBitCount() + blockBitCount() + 2);
}

uint32_t TimedCache::indexBits(uint32_t addr) const {
  return (addr>> (blockBitCount() + 2)) & ((1<< indexBitCount()) - 1);
}

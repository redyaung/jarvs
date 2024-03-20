#ifndef SIMULATOR_UTIL_HPP
#define SIMULATOR_UTIL_HPP

#include <cstdint>
#include <algorithm>

constexpr uint32_t nbytes(uint32_t nwords) {
  return nwords * 4;
}

constexpr uint32_t extractBits(uint32_t val, int start, int end) {
  uint32_t mask = ((1u << (end - start + 1)) - 1)<< start;
  return (val & mask) >> start;
}

constexpr uint32_t placeBits(uint32_t source, int start, int end, uint32_t bits) {
  uint32_t mask = (1u << (end - start + 1)) - 1;
  uint32_t emptiedSource = source & ~(mask<< start);
  return emptiedSource | ((bits & mask)<< start);
}

template<typename Collection, typename T>
inline bool contains(Collection col, const T& element) {
  return std::find(col.begin(), col.end(), element) != col.end();
}

#endif

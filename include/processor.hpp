#ifndef SIMULATOR_PROCESSOR_HPP
#define SIMULATOR_PROCESSOR_HPP

#include "memory.hpp"

#include <bit>
#include <algorithm>
#include <stdexcept>
#include <type_traits>

constexpr int registerCount = 32;

enum class RegisterType {
  Integer, FloatingPoint
};

// todo: use the updated Word
template<RegisterType T>
struct RegisterFile {
  using Value = std::conditional_t<T == RegisterType::Integer, uint32_t, float>;

  Word regs[registerCount];

  // zero-initialize the registers (no surprises!)
  RegisterFile() {
    std::fill(regs, regs + registerCount, Word(Value()));
  }

  Value readRegister(size_t regNum) const {
    if (regNum >= 32) {
      throw std::out_of_range("register number must be smaller than 32");
    }
    return Value(regs[regNum]);
  }

  void writeRegister(size_t regNum, Value value) {
    if (regNum >= 32) {
      throw std::out_of_range("register number must be smaller than 32");
    }
    // discard writes to x0 as it is hardcoded to integer 0
    if constexpr (T == RegisterType::Integer) {
      if (regNum == 0) return;
    }
    regs[regNum] = Word(value);
  }
};

#endif

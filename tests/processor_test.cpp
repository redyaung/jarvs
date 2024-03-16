#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include "processor.hpp"

namespace {
  constexpr float tolerance = 1e-6;

  TEST(IntegerRegisterFile, Initialization) {
    RegisterFile<RegisterType::Integer> regFile;
    EXPECT_TRUE(std::all_of(regFile.regs, regFile.regs + registerCount,
      [](const Word &word) { return uint32_t(word) == 0u; }));
  }

  TEST(IntegerRegisterFile, WriteAndRead) {
    RegisterFile<RegisterType::Integer> regFile;
    regFile.writeRegister(10, 0xFACADEu);
    EXPECT_EQ(regFile.readRegister(10), 0xFACADEu);
  }

  TEST(IntegerRegisterFile, WriteToX0Discarded) {
    RegisterFile<RegisterType::Integer> regFile;
    regFile.writeRegister(0, 0xFACADEu);
    EXPECT_EQ(regFile.readRegister(0), 0u);
  }

  TEST(FloatingPointRegisterFile, Initialization) {
    RegisterFile<RegisterType::FloatingPoint> regFile;
    EXPECT_TRUE(std::all_of(regFile.regs, regFile.regs + registerCount,
      [](const Word &word) { return std::fabsf(float(word)) < tolerance; }));
  }

  TEST(FloatingPointRegisterFile, WriteAndRead) {
    RegisterFile<RegisterType::FloatingPoint> regFile;
    regFile.writeRegister(10, 3.2f);
    EXPECT_FLOAT_EQ(regFile.readRegister(10), 3.2f);
  }

}


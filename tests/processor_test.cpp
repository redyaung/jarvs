#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <gmock/gmock.h>
#include "processor.hpp"

namespace {
  constexpr float tolerance = 1e-6;

  struct MockUnit : public Unit {
    // default initializer has to be brace or equals initializer
    InputSignal in1{this}, in2{this};

    MOCK_METHOD(void, notifyInputChange, (), (override));
    MOCK_METHOD(void, operate, (), (override));
  };

  TEST(IntegerRegisterFileTest, Initialization) {
    RegisterFile<RegisterType::Integer> regFile;
    EXPECT_TRUE(std::all_of(regFile.regs, regFile.regs + registerCount,
      [](const Word &word) { return uint32_t(word) == 0u; }));
  }

  TEST(IntegerRegisterFileTest, WriteAndRead) {
    RegisterFile<RegisterType::Integer> regFile;
    regFile.writeRegister(10, 0xFACADEu);
    EXPECT_EQ(regFile.readRegister(10), 0xFACADEu);
  }

  TEST(IntegerRegisterFileTest, WriteToX0Discarded) {
    RegisterFile<RegisterType::Integer> regFile;
    regFile.writeRegister(0, 0xFACADEu);
    EXPECT_EQ(regFile.readRegister(0), 0u);
  }

  TEST(FloatingPointRegisterFileTest, Initialization) {
    RegisterFile<RegisterType::FloatingPoint> regFile;
    EXPECT_TRUE(std::all_of(regFile.regs, regFile.regs + registerCount,
      [](const Word &word) { return std::fabsf(float(word)) < tolerance; }));
  }

  TEST(FloatingPointRegisterFileTest, WriteAndRead) {
    RegisterFile<RegisterType::FloatingPoint> regFile;
    regFile.writeRegister(10, 3.2f);
    EXPECT_FLOAT_EQ(regFile.readRegister(10), 3.2f);
  }

  TEST(SignalsTest, ChangePropagation) {
    MockUnit receiver;
    OutputSignal out;
    out >> receiver.in1 >> receiver.in2;
    // once from in1, once from in2
    EXPECT_CALL(receiver, notifyInputChange()).Times(2);
    out << 0xCADu;
  }

}


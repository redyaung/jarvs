#include "gmock/gmock-cardinalities.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <gmock/gmock.h>
#include "processor.hpp"

namespace {
  using ::testing::AtLeast;

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

  TEST(RegisterFileUnitTest, BasicOperation) {
    OutputSignal read1, read2, write, writeData, regWrite;
    RegisterFileUnit registers;
    MockUnit receiver;

    // hook up signals and set up initial register values
    read1 >> registers.readRegister1;
    read2 >> registers.readRegister2;
    write >> registers.writeRegister;
    writeData >> registers.writeData;
    regWrite >> registers.ctrlRegWrite;
    registers.readData1 >> receiver.in1;
    registers.readData2 >> receiver.in2;
    registers.intRegs.regs[10u] = 0xDEADBEEFu;

    // use AtLeast as if there are n signals between A and B, B is going to be
    // notified n times whenever there is a change in A
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    read1 << 10u;
    EXPECT_EQ(receiver.in1.val, 0xDEADBEEFu);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    regWrite<< 1; writeData << 0xFACADEu; write << 10u;
    EXPECT_EQ(receiver.in1.val, 0xFACADEu);
  }

  struct ImmediateGeneratorUnitTest : public testing::Test {
  protected:
    void SetUp() override {
      instr >> immGen.instruction;
      immGen.immediate >> receiver.in1;
    }

    OutputSignal instr;
    ImmediateGenerator immGen;
    MockUnit receiver;
  };

  // see Patterson-Hennessy section 2.5 (pg 93)
  TEST_F(ImmediateGeneratorUnitTest, ITypeInstructions) {
    uint32_t addi = 0b001111101000'00010'000'00001'0010011;   // addi x1, x2, 1000
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << addi;
    EXPECT_EQ(receiver.in1.val, 1000);

    uint32_t lw = 0b001111101000'00010'010'00001'0000011;     // lw x1, 1000(x2)
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << lw;
    EXPECT_EQ(receiver.in1.val, 1000);
  }

  // see Patterson-Hennessy section 2.5 (pg 93)
  TEST_F(ImmediateGeneratorUnitTest, STypeInstructions) {
    uint32_t sw = 0b0011111'00001'00010'010'01000'0100011;    // sw x1, 1000(x2)
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << sw;
    EXPECT_EQ(receiver.in1.val, 1000);
  }

  TEST(MultiplexerTest, BasicOperation) {
    OutputSignal in0, in1, ctrl;
    Multiplexer mult;
    MockUnit receiver;

    in0 >> mult.input0;
    in1 >> mult.input1;
    ctrl >> mult.control;
    mult.output >> receiver.in1;

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 0xDEADBEEFu; in1 << 0xFACADEu; ctrl << 0x0u;
    EXPECT_EQ(receiver.in1.val, 0xDEADBEEFu);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    ctrl << 0x1u;
    EXPECT_EQ(receiver.in1.val, 0xFACADEu);
  }
}


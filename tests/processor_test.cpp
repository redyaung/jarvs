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

  // see Patterson-Hennessy fig 4.12 (pg 270) for instruction encodings
  struct ALUControlTest : public testing::Test {
  protected:
    void SetUp() override {
      instr >> aluCtrl.instruction;
      aluOp >> aluCtrl.ctrlAluOp;
      aluCtrl.aluOp >> receiver.in1;
    }

    OutputSignal instr, aluOp;
    ALUControl aluCtrl;
    MockUnit receiver;
  };

  TEST_F(ALUControlTest, Add) {
    uint32_t add = 0b0000000'00011'00010'000'00001'0110011;
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << add; aluOp << 0b10u;
    EXPECT_EQ(receiver.in1.val, uint32_t(ALUOp::Add));
  }

  TEST_F(ALUControlTest, Sub) {
    uint32_t sub = 0b0100000'00011'00010'000'00001'0110011;
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << sub; aluOp << 0b10u;
    EXPECT_EQ(receiver.in1.val, uint32_t(ALUOp::Sub));
  }

  TEST_F(ALUControlTest, Addi) {
    uint32_t addi = 0b001111101000'00010'000'00001'0010011;
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << addi; aluOp << 0b10u;
    EXPECT_EQ(receiver.in1.val, uint32_t(ALUOp::Add));
  }

  TEST_F(ALUControlTest, Lw) {
    uint32_t lw = 0b001111101000'00010'010'00001'0000011;
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << lw; aluOp << 0b00u;
    EXPECT_EQ(receiver.in1.val, uint32_t(ALUOp::Add));
  }

  // beq: opcode = 1100011, func3 = 0 -- imm = 0, x1, x2
  TEST_F(ALUControlTest, Beq) {
    uint32_t beq = 0b0000000'00001'00010'000'00000'1100011;
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << beq; aluOp << 0b01u;
    EXPECT_EQ(receiver.in1.val, uint32_t(ALUOp::Sub));
  }

  struct ALUUnitTest : public testing::Test {
  protected:
    void SetUp() override {
      in0 >> alu.input0;
      in1 >> alu.input1;
      op >> alu.aluOp;
      alu.output >> receiver.in1;
      alu.zero >> receiver.in2;
    }

    OutputSignal in0, in1, op;
    ALUUnit alu;
    MockUnit receiver;
  };

  TEST_F(ALUUnitTest, ArithmeticOperations) {
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 14; in1 << 8; op << uint32_t(ALUOp::Add);
    EXPECT_EQ(receiver.in1.val, 22);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 14; in1 << 8; op << uint32_t(ALUOp::Sub);
    EXPECT_EQ(receiver.in1.val, 6);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 8; in1 << 14; op << uint32_t(ALUOp::Sub);
    EXPECT_EQ(int(receiver.in1.val), -6);
  }

  TEST_F(ALUUnitTest, LogicalOperations) {
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 1u; in1 << 0u; op << uint32_t(ALUOp::And);
    EXPECT_EQ(receiver.in1.val, 0);   // 1 & 0 = 0

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 1u; in1 << 1u; op << uint32_t(ALUOp::And);
    EXPECT_EQ(receiver.in1.val, 1);   // 1 & 1 = 1

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 1u; in1 << 0u; op << uint32_t(ALUOp::Or);
    EXPECT_EQ(receiver.in1.val, 1);   // 1 | 0 = 1
  }

  TEST_F(ALUUnitTest, ZeroOutput) {
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 8; in1 << 8; op << uint32_t(ALUOp::Sub);
    EXPECT_EQ(receiver.in1.val, 0);
    EXPECT_EQ(receiver.in2.val, 1);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 0; in1 << 1; op << uint32_t(ALUOp::And);
    EXPECT_EQ(receiver.in1.val, 0);
    EXPECT_EQ(receiver.in2.val, 1);
  }

  struct DataMemoryUnitTest : public testing::Test {
  protected:
    void SetUp() override {
      addr >> memUnit.address;
      write >> memUnit.writeData;
      willRead >> memUnit.ctrlMemRead;
      willWrite >> memUnit.ctrlMemWrite;
      memUnit.readData >> receiver.in1;
    }

    OutputSignal addr, write, willRead, willWrite;
    DataMemoryUnit memUnit;
    MockUnit receiver;
  };

  TEST_F(DataMemoryUnitTest, DoNothingOnDeassertedControlSignals) {
    EXPECT_CALL(receiver, notifyInputChange()).Times(0);
    willRead << 0; willWrite << 0;
    addr << 0xA0; write << 0xDEADBEEFu;
    EXPECT_EQ(memUnit.memory.readBlock<1>(0xA0)[0], 0x0u);
  }

  // current behavior -- likely to change in the future (see class definition)
  TEST_F(DataMemoryUnitTest, WriteToMemory) {
    EXPECT_CALL(receiver, notifyInputChange()).Times(0);
    willWrite << 1; addr << 0xA0; write << 0xDEADBEEFu;
    EXPECT_EQ(memUnit.memory.readBlock<1>(0xA0)[0], 0xDEADBEEFu);
  }

  TEST_F(DataMemoryUnitTest, ReadFromMemory) {
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    memUnit.memory.writeBlock(0xA0, Block<1>{0xFACADEu});
    willRead << 1; addr << 0xA0;
    EXPECT_EQ(receiver.in1.val, 0xFACADEu);
  }

  TEST(AndGateTest, BasicOperation) {
    OutputSignal in0, in1;
    AndGate andGate;
    MockUnit receiver;

    in0 >> andGate.input0;
    in1 >> andGate.input1;
    andGate.output >> receiver.in1;

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 1u; in1 << 1u;
    EXPECT_EQ(receiver.in1.val, 1u);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 0u; in1 << 1u;
    EXPECT_EQ(receiver.in1.val, 0u);
  }
}


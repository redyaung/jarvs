#include <array>
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <gmock/gmock.h>
#include <gmock/gmock-cardinalities.h>
#include <span>
#include <sstream>
#include "processor.hpp"
#include "assembler.hpp"

namespace {
  using ::testing::AtLeast;

  constexpr float tolerance = 1e-6;

  struct MockUnit : public Unit {
    // default initializer has to be brace or equals initializer
    InputSignal in1{this}, in2{this}, in3{this}, in4{this}, in5{this};

    MOCK_METHOD(void, notifyInputChange, (), (override));
    MOCK_METHOD(void, operate, (), (override));
  };

  void registerInstructions(PipelinedProcessor &processor, std::span<std::string> instructions) {
    for (auto i = 0; i < instructions.size(); ++i) {
      Word encoded = encodeInstruction(instructions[i]);
      processor.instructionMemory.memory._writeBlockTillDone(i * 4, Block{encoded});
    }
  }

  void registerEncodedInstructions(
    PipelinedProcessor &processor, std::span<Word> instructions
  ) {
    for (auto i = 0; i < instructions.size(); ++i) {
      processor.instructionMemory.memory._writeBlockTillDone(i * 4, Block{instructions[i]});
    }
  }

  // can accomodate loops
  void executeInstructionsFixed(
    PipelinedProcessor &processor,
    int cycleCount,
    bool debug = true
  ) {
    for (auto cycle = 0; cycle < cycleCount; cycle++) {
      processor.executeOneCycle();
      if (debug) {
        std::cout << processor << std::endl;
      }
    }
  }

  // note: cannot handle loops
  // skipCount: # instructions skipped due to jumps or conditional branches
  void executeInstructions(
    PipelinedProcessor &processor,
    int instructionCount,
    int skipCount = 0,
    int stallCount = 0,
    bool debug = true
  ) {
    int cycleCount = 4 + instructionCount - skipCount + stallCount;
    executeInstructionsFixed(processor, cycleCount, debug);
  }

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

  TEST(DecodeUnitTest, DecodeRTypeInstruction) {
    OutputSignal instr;
    DecodeUnit decoder;
    MockUnit receiver;

    instr >> decoder.instruction;
    decoder.readRegister1 >> receiver.in1;
    decoder.readRegister2 >> receiver.in2;
    decoder.writeRegister >> receiver.in3;
    decoder.func3 >> receiver.in4;
    decoder.func7 >> receiver.in5;

    uint32_t add = 0b0000000'00011'00010'000'00001'0110011;   // add x1, x2, x3
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << add;
    EXPECT_EQ(*receiver.in1, 2);
    EXPECT_EQ(*receiver.in2, 3);
    EXPECT_EQ(*receiver.in3, 1);
    EXPECT_EQ(*receiver.in4, 0x0);
    EXPECT_EQ(*receiver.in5, 0x0);
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
    EXPECT_EQ(*receiver.in1, 0xDEADBEEFu);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    regWrite<< 1; writeData << 0xFACADEu; write << 10u;
    EXPECT_EQ(*receiver.in1, 0xFACADEu);
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
  TEST_F(ImmediateGeneratorUnitTest, AluITypeInstructions) {
    uint32_t addi = 0b001111101000'00010'000'00001'0010011;   // addi x1, x2, 1000
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << addi;
    EXPECT_EQ(*receiver.in1, 1000);
  }

  TEST_F(ImmediateGeneratorUnitTest, LoadITypeInstructions) {
    uint32_t lw = 0b001111101000'00010'010'00001'0000011;     // lw x1, 1000(x2)
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << lw;
    EXPECT_EQ(*receiver.in1, 1000);
  }

  // depends on correctness of assembler's jalr encoding (jal is similar)
  TEST_F(ImmediateGeneratorUnitTest, JalrITypeInstruction) {
    auto jalr = encodeInstruction("jalr x0, 16(x2)");
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << jalr;
    EXPECT_EQ(*receiver.in1, 16);
  }

  // see Patterson-Hennessy section 2.5 (pg 93)
  TEST_F(ImmediateGeneratorUnitTest, STypeInstructions) {
    uint32_t sw = 0b0011111'00001'00010'010'01000'0100011;    // sw x1, 1000(x2)
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << sw;
    EXPECT_EQ(*receiver.in1, 1000);
  }

  TEST_F(ImmediateGeneratorUnitTest, UTypeInstructions) {
    // todo: fill in body after implementing lui
  }

  TEST_F(ImmediateGeneratorUnitTest, UJTypeInstructions) {
    auto jal = encodeInstruction("jal x0, 32");
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << jal;
    EXPECT_EQ(*receiver.in1, 32);
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
    EXPECT_EQ(*receiver.in1, 0xDEADBEEFu);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    ctrl << 0x1u;
    EXPECT_EQ(*receiver.in1, 0xFACADEu);
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
    EXPECT_EQ(*receiver.in1, uint32_t(ALUOp::Add));
  }

  TEST_F(ALUControlTest, Sub) {
    uint32_t sub = 0b0100000'00011'00010'000'00001'0110011;
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << sub; aluOp << 0b10u;
    EXPECT_EQ(*receiver.in1, uint32_t(ALUOp::Sub));
  }

  TEST_F(ALUControlTest, Addi) {
    uint32_t addi = 0b001111101000'00010'000'00001'0010011;
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << addi; aluOp << 0b10u;
    EXPECT_EQ(*receiver.in1, uint32_t(ALUOp::Add));
  }

  TEST_F(ALUControlTest, Lw) {
    uint32_t lw = 0b001111101000'00010'010'00001'0000011;
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << lw; aluOp << 0b00u;
    EXPECT_EQ(*receiver.in1, uint32_t(ALUOp::Add));
  }

  // (unused) since branch logic has been moved to the decode stage
  // beq: opcode = 1100011, func3 = 0 -- imm = 0, x1, x2
  TEST_F(ALUControlTest, Beq) {
    uint32_t beq = 0b0000000'00001'00010'000'00000'1100011;
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    instr << beq; aluOp << 0b01u;
    EXPECT_EQ(*receiver.in1, uint32_t(ALUOp::Sub));
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
    EXPECT_EQ(*receiver.in1, 22);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 14; in1 << 8; op << uint32_t(ALUOp::Sub);
    EXPECT_EQ(*receiver.in1, 6);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 8; in1 << 14; op << uint32_t(ALUOp::Sub);
    EXPECT_EQ(int(*receiver.in1), -6);
  }

  TEST_F(ALUUnitTest, LogicalOperations) {
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 1u; in1 << 0u; op << uint32_t(ALUOp::And);
    EXPECT_EQ(*receiver.in1, 0);   // 1 & 0 = 0

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 1u; in1 << 1u; op << uint32_t(ALUOp::And);
    EXPECT_EQ(*receiver.in1, 1);   // 1 & 1 = 1

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 1u; in1 << 0u; op << uint32_t(ALUOp::Or);
    EXPECT_EQ(*receiver.in1, 1);   // 1 | 0 = 1
  }

  TEST_F(ALUUnitTest, ZeroOutput) {
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 8; in1 << 8; op << uint32_t(ALUOp::Sub);
    EXPECT_EQ(*receiver.in1, 0);
    EXPECT_EQ(*receiver.in2, 1);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 0; in1 << 1; op << uint32_t(ALUOp::And);
    EXPECT_EQ(*receiver.in1, 0);
    EXPECT_EQ(*receiver.in2, 1);
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
    DataMemoryUnit memUnit{std::shared_ptr<TimedMemory>(
      new TimedMainMemory{8, 1}
    )};
    MockUnit receiver;
  };

  TEST_F(DataMemoryUnitTest, DoNothingOnDeassertedControlSignals) {
    EXPECT_CALL(receiver, notifyInputChange()).Times(0);
    willRead << 0; willWrite << 0; addr << 0xA0; write << 0xDEADBEEFu;
    memUnit.operate();
    EXPECT_EQ(memUnit.memory->_readBlockTillDone(0xA0, 1)[0], 0x0u);
  }

  // current behavior -- likely to change in the future (see class definition)
  TEST_F(DataMemoryUnitTest, WriteToMemory) {
    EXPECT_CALL(receiver, notifyInputChange()).Times(0);
    willWrite << 1; addr << 0xA0; write << 0xDEADBEEFu;
    memUnit.operate();
    EXPECT_EQ(memUnit.memory->_readBlockTillDone(0xA0, 1)[0], 0xDEADBEEFu);
  }

  TEST_F(DataMemoryUnitTest, ReadFromMemory) {
    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    memUnit.memory->_writeBlockTillDone(0xA0, Block{0xFACADEu});
    willRead << 1; addr << 0xA0;
    memUnit.operate();
    EXPECT_EQ(*receiver.in1, 0xFACADEu);
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
    EXPECT_EQ(*receiver.in1, 1u);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    in0 << 0u; in1 << 1u;
    EXPECT_EQ(*receiver.in1, 0u);
  }

  // here, we use MEM/WB pipeline registers but this choice is arbitrary
  TEST(PipelineRegisterTest, PropagatesOnlyOnClockEdge) {
    OutputSignal read, alu;
    MEMWBRegisters pRegisters;
    MockUnit receiver;

    read >> pRegisters.readMemoryDataIn;
    alu >> pRegisters.aluOutputIn;
    pRegisters.readMemoryDataOut >> receiver.in1;
    pRegisters.aluOutputOut >> receiver.in2;

    EXPECT_CALL(receiver, notifyInputChange()).Times(0);
    read << 0xDEADu; alu << 0xFACADEu;
    EXPECT_EQ(*receiver.in1, 0x0u);
    EXPECT_EQ(*receiver.in2, 0x0u);

    EXPECT_CALL(receiver, notifyInputChange()).Times(AtLeast(1));
    pRegisters.operate();   // normally called by a processor
    EXPECT_EQ(*receiver.in1, 0xDEADu);
    EXPECT_EQ(*receiver.in2, 0xFACADEu);
  }

  TEST(PipelinedProcessorTest, AddInstruction) {
    PipelinedProcessor processor;

    uint32_t add = 0b0000000'00011'00010'000'00001'0110011;   // add x1, x2, x3
    processor.instructionMemory.memory._writeBlockTillDone(0x0, Block{add});
    processor.registers.intRegs.writeRegister(2, 6);
    processor.registers.intRegs.writeRegister(3, 7);

    // execute for 5 cycles -- enough for write back stage of first instruction to complete
    for (int cycle = 0; cycle < 5; cycle++) {
      processor.executeOneCycle();
    }
    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 13);
  }

  TEST(PipelinedProcessorTest, SubInstruction) {
    PipelinedProcessor processor;

    uint32_t sub = 0b0100000'00011'00010'000'00001'0110011;   // sub x1, x2, x3
    processor.instructionMemory.memory._writeBlockTillDone(0x0, Block{sub});
    processor.registers.intRegs.writeRegister(2, 6);
    processor.registers.intRegs.writeRegister(3, 7);

    for (int cycle = 0; cycle < 5; cycle++) {
      processor.executeOneCycle();
    }
    EXPECT_EQ(processor.registers.intRegs.readRegister(1), -1);
  }

  // newly added tests; uses the assembler -- more of integration tests rather than unit tests
  TEST(PipelinedProcessorTest, OrInstruction) {
    PipelinedProcessor processor;

    processor.registers.intRegs.writeRegister(11, 1u);
    processor.registers.intRegs.writeRegister(12, 0u);
    std::vector<std::string> instructions {
      "or x10, x11, x12"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size());

    EXPECT_EQ(processor.registers.intRegs.readRegister(10), 1u);
  }

  TEST(PipelinedProcessorTest, AndInstruction) {
    PipelinedProcessor processor;

    processor.registers.intRegs.writeRegister(11, 1u);
    processor.registers.intRegs.writeRegister(12, 0u);
    std::vector<std::string> instructions {
      "and x10, x11, x12"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size());

    EXPECT_EQ(processor.registers.intRegs.readRegister(10), 0u);
  }

  TEST(PipelinedProcessorTest, ShiftLeftLogicalInstruction) {
    PipelinedProcessor processor;

    processor.registers.intRegs.writeRegister(11, 1u);
    processor.registers.intRegs.writeRegister(12, 3u);
    std::vector<std::string> instructions {
      "sll x10, x11, x12"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size());

    EXPECT_EQ(processor.registers.intRegs.readRegister(10), 8u);
  }

  TEST(PipelinedProcessorTest, ShiftRightLogicalInstruction) {
    PipelinedProcessor processor;

    processor.registers.intRegs.writeRegister(11, 0b1101u);
    processor.registers.intRegs.writeRegister(12, 2u);
    std::vector<std::string> instructions {
      "srl x10, x11, x12"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size());

    EXPECT_EQ(processor.registers.intRegs.readRegister(10), 0b11u);
  }

  TEST(PipelinedProcessorTest, AddImmediateInstruction) {
    PipelinedProcessor processor;

    uint32_t addi = 0b001111101000'00010'000'00001'0010011;   // addi x1, x2, 1000
    processor.instructionMemory.memory._writeBlockTillDone(0x0, Block{addi});
    processor.registers.intRegs.writeRegister(2, 24u);

    for (int cycle = 0; cycle < 5; cycle++) {
      processor.executeOneCycle();
    }
    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 1024u);
  }

  TEST(PipelinedProcessorTest, AndImmediateInstruction) {
    PipelinedProcessor processor;

    processor.registers.intRegs.writeRegister(11, 0u);
    std::vector<std::string> instructions {
      "andi x10, x11, 1"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size());

    EXPECT_EQ(processor.registers.intRegs.readRegister(10), 0u);
  }

  TEST(PipelinedProcessorTest, LoadInstruction) {
    PipelinedProcessor processor;

    // load the value at memory address 0x10 + 4 = 0x14 into x1
    uint32_t lw = 0b000000000100'00010'010'00001'0000011;     // lw x1, 4(x2)
    processor.instructionMemory.memory._writeBlockTillDone(0x0, Block{lw});
    processor.registers.intRegs.writeRegister(2, 0x10); 
    processor.dataMemory.memory->_writeBlockTillDone(0x14, Block{0xBEEFu});

    for (int cycle = 0; cycle < 5; cycle++) {
      processor.executeOneCycle();
    }
    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 0xBEEFu);
  }

  TEST(PipelinedProcessorTest, StoreInstruction) {
    PipelinedProcessor processor;

    // store 0xFACADEu at the memory address 0x10 + 4 = 0x14
    uint32_t sw = 0b0000000'00001'00010'010'00100'0100011;    // sw x1, 4(x2)
    processor.instructionMemory.memory._writeBlockTillDone(0x0, Block{sw});
    processor.registers.intRegs.writeRegister(1, 0xFACADEu); 
    processor.registers.intRegs.writeRegister(2, 0x10u);

    // only need the MEM stage to complete
    for (int cycle = 0; cycle < 4; cycle++) {
      processor.executeOneCycle();
    }
    EXPECT_EQ(processor.dataMemory.memory->_readBlockTillDone(0x14u, 1)[0], 0xFACADEu);
  }

  TEST(PipelinedProcessorTest, MultipleAddInstructions) {
    PipelinedProcessor processor;

    processor.registers.intRegs.writeRegister(2, 2);
    processor.registers.intRegs.writeRegister(3, 3);
    processor.registers.intRegs.writeRegister(12, 12);
    processor.registers.intRegs.writeRegister(13, 13);
    std::vector<std::string> instructions {
      "add x1, x2, x3",
      "add x11, x12, x13",
      "add x0, x0, x0",   // nop
      "add x0, x0, x0",   // nop
      "add x21, x1, x11"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size());

    // x1 = x2 + x3 = 2 + 3 = 5
    // x11 = x12 + x13 = 12 + 13 = 25
    // x21 = x1 + x11 = 5 + 25 = 30
    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 5);
    EXPECT_EQ(processor.registers.intRegs.readRegister(11), 25);
    EXPECT_EQ(processor.registers.intRegs.readRegister(21), 30);
  }

  TEST(PipelinedProcessorTest, LoadAddSequence) {
    PipelinedProcessor processor;

    processor.dataMemory.memory->_writeBlockTillDone(0x0, Block{1u});
    processor.dataMemory.memory->_writeBlockTillDone(0x4, Block{2u});
    std::vector<std::string> instructions {
      "lw x1, 0(x0)",
      "lw x2, 4(x0)",
      "add x0, x0, x0",
      "add x0, x0, x0",
      "add x3, x1, x2"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size());

    // x1 = LOAD(0x0) = 1
    // x2 = LOAD(0x4) = 2
    // x3 = x1 + x2 = 1 + 2 = 3
    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 1);
    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 2);
    EXPECT_EQ(processor.registers.intRegs.readRegister(3), 3);
  }

  TEST(PipelinedProcessorTest, StoreLoadSequence) {
    PipelinedProcessor processor;

    std::vector<std::string> instructions {
      "addi x1, x0, 80",
      "add x0, x0, x0",
      "add x0, x0, x0",
      "sw x1, 0(x0)",
      "lw x2, 0(x0)"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size());

    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 80);
    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 80);
  }

  TEST(PipelinedProcessorTest, ForwardFromEX_MEM) {
    PipelinedProcessor processor(true);

    std::vector<std::string> instructions {
      "addi x1, x0, 2",
      "addi x2, x1, 3"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size());

    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 2);
    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 5);
  }

  TEST(PipelinedProcessorTest, ForwardFromMEM_WB) {
    PipelinedProcessor processor(true);

    processor.dataMemory.memory->_writeBlockTillDone(0x0, Block{24});
    std::vector<std::string> instructions {
      "lw x1, 0(x0)",
      "add x0, x0, x0",   // nop
      "add x2, x1, x1"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size());

    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 24);
    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 48);
  }

  TEST(PipelinedProcessorTest, HandleLoadUseHazard) {
    PipelinedProcessor processor(true);

    processor.dataMemory.memory->_writeBlockTillDone(0x0, Block{24});
    std::vector<std::string> instructions {
      "lw x1, 0(x0)",
      "add x2, x1, x1"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size(), 0, 1);

    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 24);
    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 48);
  }

  TEST(PipelinedProcessorTest, HandleLoadUseHazard2) {
    PipelinedProcessor processor(true);

    processor.dataMemory.memory->_writeBlockTillDone(0x0, Block{24});
    std::vector<std::string> instructions {
      "lw x1, 0(x0)",
      "add x2, x1, x1",
      "add x2, x2, x2"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size(), 0, 1);

    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 24);
    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 96);
  }

  TEST(PipelinedProcessorTest, HazardDetectionNoFwdLoadAdd) {
    PipelinedProcessor processor;

    processor.dataMemory.memory->_writeBlockTillDone(0x0, Block{1u, 2u});
    std::vector<std::string> instructions {
      "lw x1, 0(x0)",
      "lw x2, 4(x0)",
      "add x3, x1, x2"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size(), 0, 2);

    // x1 = LOAD(0x0) = 1
    // x2 = LOAD(0x4) = 2
    // x3 = x1 + x2 = 1 + 2 = 3
    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 1);
    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 2);
    EXPECT_EQ(processor.registers.intRegs.readRegister(3), 3);
  }

  TEST(PipelinedProcessorTest, HazardDetectionNoFwdAdds) {
    PipelinedProcessor processor;

    std::vector<std::string> instructions {
      "addi x1, x0, 1",
      "add x2, x1, x1"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size(), 0, 2);

    // x1 = LOAD(0x0) = 1
    // x2 = LOAD(0x4) = 2
    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 1);
    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 2);
  }

  TEST(PipelinedProcessorTest, ConditionalBranchTaken) {
    PipelinedProcessor processor(true);

    std::vector<std::string> instructions {
      "beq x0, x0, 12",   // jump 2 instructions ahead
      "addi x1, x0, 1",   // should flush this one
      "addi x2, x0, 2",   // should also jump over this one
      "addi x3, x0, 3"    // << land here
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size(), 1);

    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 0);
    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 0);
    EXPECT_EQ(processor.registers.intRegs.readRegister(3), 3);
  }

  TEST(PipelinedProcessorTest, ConditionalBranchNotTaken) {
    PipelinedProcessor processor(true);

    std::vector<std::string> instructions {
      "addi x1, x0, 1",
      "add x0, x0, x0",   // nop    
      "add x0, x0, x0",   // nop    
      "beq x0, x1, 8",    // this branch won't be taken
      "addi x2, x0, 1",
      "addi x3, x0, 1"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size());

    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 1);
    EXPECT_EQ(processor.registers.intRegs.readRegister(3), 1);
  }

  TEST(PipelinedProcessorTest, JumpAndLink) {
    PipelinedProcessor processor(true);

    std::vector<std::string> instructions {
      "jal x1, 8",
      "add x0, x0, x0",
      "addi x10, x0, 3"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size(), 1, 1);

    // earlier mistake: x1 should contain the next instruction after jal (0x0) which is 0x4
    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 4);
    EXPECT_EQ(processor.registers.intRegs.readRegister(10), 3);
  }

  TEST(PipelinedProcessorTest, JumpAndLinkBackwards) {
    PipelinedProcessor processor(true);

    processor.registers.intRegs.writeRegister(10, 0);
    std::vector<std::string> instructions {
      "addi x10, x10, 10",
      "add x10, x10, x10",
      "jal x0, -8",
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size() + 2, 0, 1);

    EXPECT_EQ(processor.registers.intRegs.readRegister(10), 60);
  }

  TEST(PipelinedProcessorTest, JumpAndLinkRegister) {
    PipelinedProcessor processor(true);

    // x1 = 12 (4th instruction)
    processor.registers.intRegs.writeRegister(1, 12);
    std::vector<std::string> instructions {
      "jalr x0, 0(x1)",
      "addi x10, x0, 1",
      "addi x11, x0, 1",
      "addi x12, x0, 1"
    };
    registerInstructions(processor, instructions);
    // stall count is not mutually exclusive of skip count
    //  -- here, 2nd instruction is stalled and skipped
    executeInstructions(processor, instructions.size(), 2, 1);

    EXPECT_EQ(processor.registers.intRegs.readRegister(10), 0);
    EXPECT_EQ(processor.registers.intRegs.readRegister(11), 0);
    EXPECT_EQ(processor.registers.intRegs.readRegister(12), 1);
  }

  // caution: check test for correctness before searching for bugs!
  TEST(PipelinedProcessorTest, ConditionalBranchTaken_Bne) {
    PipelinedProcessor processor(true);

    processor.registers.intRegs.writeRegister(1, 1);
    std::vector<std::string> instructions {
      "bne x0, x1, 12",   // jump 2 instructions ahead
      "addi x10, x0, 1",   // should flush this one
      "addi x11, x0, 2",   // should also jump over this one
      "addi x12, x0, 3"    // << land here
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size(), 2, 1);

    EXPECT_EQ(processor.registers.intRegs.readRegister(10), 0);
    EXPECT_EQ(processor.registers.intRegs.readRegister(11), 0);
    EXPECT_EQ(processor.registers.intRegs.readRegister(12), 3);
  }

  TEST(PipelinedProcessorTest, ConditionalBranchNotTaken_Blt) {
    PipelinedProcessor processor(true);

    processor.registers.intRegs.writeRegister(1, 1);
    std::vector<std::string> instructions {
      "blt x1, x0, 8",    // this branch won't be taken
      "addi x2, x0, 1",
      "addi x3, x0, 1"
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size());

    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 1);
    EXPECT_EQ(processor.registers.intRegs.readRegister(3), 1);
  }

  TEST(PipelinedProcessorTest, ConditionalBranchTaken_Bge) {
    PipelinedProcessor processor(true);

    processor.registers.intRegs.writeRegister(1, 1);
    std::vector<std::string> instructions {
      "bge x1, x0, 12",   // jump 2 instructions ahead
      "addi x10, x0, 1",   // should flush this one
      "addi x11, x0, 2",   // should also jump over this one
      "addi x12, x0, 3"    // << land here
    };
    registerInstructions(processor, instructions);
    executeInstructions(processor, instructions.size(), 2, 1);

    EXPECT_EQ(processor.registers.intRegs.readRegister(10), 0);
    EXPECT_EQ(processor.registers.intRegs.readRegister(11), 0);
    EXPECT_EQ(processor.registers.intRegs.readRegister(12), 3);
  }

  // ref: used the terminal version of the simulator to manually run loop.asm
  TEST(PipelinedProcessorIntegrationTest, SimpleLoop) {
    PipelinedProcessor processor(true);
    std::string instructionsStr(R"(
      addi x10, x0, 1
      sw x10, 0(x0)

      addi x11, x0, 2
      sw x11, 4(x0)

      addi x12, x0, 3
      sw x12, 8(x0)

      add x21, x0, x0

      add x10, x0, x0
      addi x11, x0, 12

      add x0, x0, x0
      add x0, x0, x0

      bge x10, x11, 20
      lw x20, 0(x10)
      add x21, x21, x20
      addi x10, x10, 4
      jal x0, -16

      sw x21, 12(x0)
    )");
    std::stringstream stream(instructionsStr);
    auto instructions = encodeInstructions(stream);
    registerEncodedInstructions(processor, instructions);
    executeInstructionsFixed(processor, 40, false);

    EXPECT_EQ(processor.registers.intRegs.readRegister(10), 12);
    EXPECT_EQ(processor.registers.intRegs.readRegister(11), 12);
    EXPECT_EQ(processor.registers.intRegs.readRegister(20), 3);
    EXPECT_EQ(processor.registers.intRegs.readRegister(21), 6);
    EXPECT_EQ(processor.dataMemory.memory->_readBlockTillDone(0, 1)[0], 1);
    EXPECT_EQ(processor.dataMemory.memory->_readBlockTillDone(4, 1)[0], 2);
    EXPECT_EQ(processor.dataMemory.memory->_readBlockTillDone(8, 1)[0], 3);
    EXPECT_EQ(processor.dataMemory.memory->_readBlockTillDone(12, 1)[0], 6);
  }

  TEST(MemoryTimingTest, Latency2Cycles) {
    PipelinedProcessor processor(true, 2);

    processor.dataMemory.memory->_writeBlockTillDone(0x0, Block{1u, 2u});
    std::string instructionsStr(R"(
      lw x1, 0(x0)
      lw x2, 4(x0)
      add x3, x1, x2
    )");
    std::stringstream stream(instructionsStr);
    auto instructions = encodeInstructions(stream);
    registerEncodedInstructions(processor, instructions);

    // MEM takes 2 cycles, so at clock cycle 6 (5 + 1), x1 = 1
    executeInstructionsFixed(processor, 6);
    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 1);

    // for 2nd lw: 6 = MEM, 7 = MEM, 8 = WB -> x2 = 2
    executeInstructionsFixed(processor, 2);
    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 2);

    // for add, 8 = EX, 9 = MEM, 10 = WB -> x3 = 1 + 2 = 3
    executeInstructionsFixed(processor, 2);
    EXPECT_EQ(processor.registers.intRegs.readRegister(3), 3);
  }

  // without forwarding, a load-use data hazard needs 2 nops, but due to memory latency being 2,
  // this nop is reduced to 1, so the entire execution takes 1 additional cycle, thus a total
  // 11 cycles
  TEST(MemoryTimingTest, Latency2CyclesNoFwd) {
    PipelinedProcessor processor(false, 2);

    processor.dataMemory.memory->_writeBlockTillDone(0x0, Block{1u, 2u});
    std::string instructionsStr(R"(
      lw x1, 0(x0)
      lw x2, 4(x0)
      add x3, x1, x2
    )");
    std::stringstream stream(instructionsStr);
    auto instructions = encodeInstructions(stream);
    registerEncodedInstructions(processor, instructions);

    executeInstructionsFixed(processor, 6);
    EXPECT_EQ(processor.registers.intRegs.readRegister(1), 1);

    // for 2nd lw: 6 = MEM, 7 = MEM, 8 = WB -> x2 = 2
    executeInstructionsFixed(processor, 2);
    EXPECT_EQ(processor.registers.intRegs.readRegister(2), 2);

    // for add, 8 = ID, 9 = EX, 10 = MEM, 11 = WB -> x3 = 1 + 2 = 3
    executeInstructionsFixed(processor, 3);
    EXPECT_EQ(processor.registers.intRegs.readRegister(3), 3);
  }
}

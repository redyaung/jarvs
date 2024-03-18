#include "processor.hpp"
#include <stdexcept>

constexpr uint32_t extractBits(uint32_t val, int start, int end) {
  uint32_t mask = ((1u << (end - start + 1)) - 1)<< start;
  return (val & mask) >> start;
}

OutputSignal& OutputSignal::operator>>(InputSignal &signal) {
  syncedInputs.push_back(&signal);
  return *this;
}

void OutputSignal::operator<<(Word newValue) {
  val = newValue;
  for (InputSignal *signal : syncedInputs) {
    signal->val = newValue;
    signal->unit->notifyInputChange();
  }
}

void DecodeUnit::operate() {
  readRegister1 << extractBits(instruction.val, 15, 19);
  readRegister2 << extractBits(instruction.val, 20, 24);
  writeRegister << extractBits(instruction.val, 7, 11);
  func3 << extractBits(instruction.val, 12, 14);
  func7 << extractBits(instruction.val, 25, 31);
}

// see Patterson-Hennessy fig 4.26 (pg 281)
void ControlUnit::operate() {
  uint32_t opcode = extractBits(instruction.val, 0, 6);
  bool isRType = (opcode == 0b0110011);
  bool isLw = (opcode == 0b0000011);
  bool isSw = (opcode == 0b0100011);
  bool isBeq = (opcode == 0b1100011);
  // for some reason, this kind of conversion may be necessary
  ctrlRegWrite << (isRType || isLw ? 0b1 : 0b0);
  ctrlAluSrc << (isLw || isSw ? 0b1 : 0b0);
  ctrlAluOp << (isBeq ? 0b01 : (isRType ? 0b10 : 0b00));
  ctrlBranch << (isBeq ? 0b1 : 0b0);
  ctrlMemWrite << (isSw ? 0b1 : 0b0);
  ctrlMemRead << (isLw ? 0b1 : 0b0);
  ctrlMemToReg << (isLw ? 0b1 : 0b0);
}

void RegisterFileUnit::operate() {
  // recall: when writes and reads are in the same cycle, writes complete first
  if (ctrlRegWrite.val) {
    intRegs.writeRegister(writeRegister.val, writeData.val);
  }
  readData1 << intRegs.readRegister(readRegister1.val);
  readData2 << intRegs.readRegister(readRegister2.val);
}

void ImmediateGenerator::operate() {
  // hardcode for now -- have to figure out how to distinguish different instruction
  // types from opcode alone
  uint32_t opcode = instruction.val & 0b1111111;      // first 7 bits
  if (opcode == 0b0010011 || opcode == 0b0000011) {   // addi or lw (I-type)
    uint32_t imm = extractBits(instruction.val, 20, 31);
    immediate << imm;
  } else if (opcode == 0b0100011) {   // sw (S-type)
    uint32_t immLow = extractBits(instruction.val, 7, 11);
    uint32_t immHigh = extractBits(instruction.val, 25, 31);
    uint32_t imm = (immHigh<< 5) + immLow;
    immediate << imm;
  } else {
    // do nothing otherwise
  }
}

void Multiplexer::operate() {
  output << (control.val == 0u ? input0.val : input1.val);
}

// refer to Patterson-Hennessy fig 4.12 (pg 270)
void ALUControl::operate() {
  if (ctrlAluOp.val == 0b00u) {         // lw or sw
    aluOp << uint32_t(ALUOp::Add);
  } else if (ctrlAluOp.val == 0b01u) {  // beq
    aluOp << uint32_t(ALUOp::Sub);
  } else if (ctrlAluOp.val == 0b10u) {  // R-type & I-type
    uint32_t func7 = extractBits(instruction.val, 25, 31);
    uint32_t func3 = extractBits(instruction.val, 12, 14);
    if (func7 == 0x20u && func3 == 0x0u) {  // sub
      aluOp << uint32_t(ALUOp::Sub);
    } else if (func3 == 0x0u) {             // add, addi
      aluOp << uint32_t(ALUOp::Add);
    }
  }
}

void ALUUnit::operate() {
  switch (ALUOp{uint32_t(aluOp.val)}) {
    case ALUOp::Add:
      output << (int(input0.val) + int(input1.val));
      break;
    case ALUOp::Sub:
      output << (int(input0.val) - int(input1.val));
      break;
    case ALUOp::And:
      output << (int(input0.val) & int(input1.val));
      break;
    case ALUOp::Or:
      output << (int(input0.val) | int(input1.val));
      break;
  }
  zero << (output.val == 0u);
}

// won't read and write at the same time
void DataMemoryUnit::operate() {
  if (ctrlMemRead.val) {
    Word readWord = memory.readBlock<1>(address.val)[0];
    readData << readWord;
  } else if (ctrlMemWrite.val) {
    memory.writeBlock(address.val, Block<1>{writeData.val});
  }
}

void InstructionMemoryUnit::operate() {
  Word readInstruction = memory.readBlock<1>(address.val)[0];
  instruction << readInstruction;
}

void AndGate::operate() {
  output << (input0.val & input1.val);
}

void IFIDRegisters::operate() {
  pcOut << pcIn.val;
  instructionOut << instructionIn.val;
}

void IDEXRegisters::operate() {
  readData1Out << readData1In.val;
  readData2Out << readData2In.val;
  immediateOut << immediateIn.val;
  instructionOut << instructionIn.val;

  ctrlAluSrcOut << ctrlAluSrcIn.val;
  ctrlAluOpOut << ctrlAluOpIn.val;
  ctrlBranchOut << ctrlBranchIn.val;
  ctrlMemWriteOut << ctrlMemWriteIn.val;
  ctrlMemReadOut << ctrlMemReadIn.val;
  ctrlMemToRegOut << ctrlMemToRegIn.val;
  ctrlRegWriteOut << ctrlRegWriteIn.val;
  writeRegisterOut << writeRegisterIn.val;
  pcOut << pcIn.val;
}

void EXMEMRegisters::operate() {
  branchAdderOutputOut << branchAdderOutputIn.val;
  zeroOut << zeroIn.val;
  aluOutputOut << aluOutputIn.val;
  readData2Out << readData2In.val;

  ctrlBranchOut << ctrlBranchIn.val;
  ctrlMemWriteOut << ctrlMemWriteIn.val;
  ctrlMemReadOut << ctrlMemReadIn.val;
  ctrlMemToRegOut << ctrlMemToRegIn.val;
  ctrlRegWriteOut << ctrlRegWriteIn.val;
  writeRegisterOut << writeRegisterIn.val;
}

void MEMWBRegisters::operate() {
  readMemoryDataOut << readMemoryDataIn.val;
  aluOutputOut << aluOutputIn.val;

  ctrlMemToRegOut << ctrlMemToRegIn.val;
  ctrlRegWriteOut << ctrlRegWriteIn.val;
  writeRegisterOut << writeRegisterIn.val;
}

void InstructionIssueUnit::operate() {
  pcOut << pcIn.val;
}

PipelinedProcessor::PipelinedProcessor() {
  synchronizeSignals();
  registerInSyncUnits();
}

void PipelinedProcessor::registerInSyncUnits() {
  syncedUnits.push_back(&MEM_WB);
  syncedUnits.push_back(&EX_MEM);
  syncedUnits.push_back(&ID_EX);
  syncedUnits.push_back(&IF_ID);
  syncedUnits.push_back(&issueUnit);
}

void PipelinedProcessor::synchronizeSignals() {
  // fetch stage
  pcAdder.output >> pcChooser.input0;
  EX_MEM.branchAdderOutputOut >> pcChooser.input1;
  branchChooser.output >> pcChooser.control;

  pcChooser.output >> issueUnit.pcIn;

  issueUnit.pcOut >> pcAdder.input0;
  pcAdder.input1.val = 4u;    // hardcoded

  issueUnit.pcOut >> instructionMemory.address;

  issueUnit.pcOut >> IF_ID.pcIn;
  instructionMemory.instruction >> IF_ID.instructionIn;

  // decode stage
  IF_ID.instructionOut >> decoder.instruction;
  IF_ID.instructionOut >> control.instruction;

  decoder.readRegister1 >> registers.readRegister1;
  decoder.readRegister2 >> registers.readRegister2;
  MEM_WB.writeRegisterOut >> registers.writeRegister;
  writeBackSrcChooser.output >> registers.writeData;
  MEM_WB.ctrlRegWriteOut >> registers.ctrlRegWrite;

  IF_ID.instructionOut >> immGen.instruction;

  control.ctrlAluOp >> ID_EX.ctrlAluOpIn;
  control.ctrlAluSrc >> ID_EX.ctrlAluSrcIn;
  control.ctrlBranch >> ID_EX.ctrlBranchIn;
  control.ctrlMemWrite >> ID_EX.ctrlMemWriteIn;
  control.ctrlMemRead >> ID_EX.ctrlMemReadIn;
  control.ctrlMemWrite >> ID_EX.ctrlMemWriteIn;
  control.ctrlMemToReg >> ID_EX.ctrlMemToRegIn;
  control.ctrlRegWrite >> ID_EX.ctrlRegWriteIn;

  IF_ID.pcOut >> ID_EX.pcIn;
  registers.readData1 >> ID_EX.readData1In;
  registers.readData2 >> ID_EX.readData2In;
  immGen.immediate >> ID_EX.immediateIn;
  IF_ID.instructionOut >> ID_EX.instructionIn;
  decoder.writeRegister >> ID_EX.writeRegisterIn;

  // execute stage
  ID_EX.pcOut >> branchAdder.input0;
  ID_EX.immediateOut >> branchAdder.input1;

  ID_EX.readData2Out >> aluSrc2Chooser.input0;
  ID_EX.immediateOut >> aluSrc2Chooser.input1;
  ID_EX.ctrlAluSrcOut >> aluSrc2Chooser.control;

  ID_EX.readData1Out >> alu.input0;
  aluSrc2Chooser.output >> alu.input1;
  aluControl.aluOp >> alu.aluOp;

  ID_EX.instructionOut >> aluControl.instruction;
  ID_EX.ctrlAluOpOut >> aluControl.ctrlAluOp;

  ID_EX.ctrlBranchOut >> EX_MEM.ctrlBranchIn;
  ID_EX.ctrlMemWriteOut >> EX_MEM.ctrlMemWriteIn;
  ID_EX.ctrlMemReadOut >> EX_MEM.ctrlMemReadIn;
  ID_EX.ctrlMemToRegOut >> EX_MEM.ctrlMemToRegIn;
  ID_EX.ctrlRegWriteOut >> EX_MEM.ctrlRegWriteIn;
  ID_EX.writeRegisterOut >> EX_MEM.writeRegisterIn;

  branchAdder.output >> EX_MEM.branchAdderOutputIn;
  alu.zero >> EX_MEM.zeroIn;
  alu.output >> EX_MEM.aluOutputIn;
  ID_EX.readData2Out >> EX_MEM.readData2In;

  // memory stage
  EX_MEM.ctrlBranchOut >> branchChooser.input0;
  EX_MEM.zeroOut >> branchChooser.input1;

  EX_MEM.aluOutputOut >> dataMemory.address;
  EX_MEM.readData2Out >> dataMemory.writeData;
  EX_MEM.ctrlMemWriteOut >> dataMemory.ctrlMemWrite;
  EX_MEM.ctrlMemReadOut >> dataMemory.ctrlMemRead;

  dataMemory.readData >> MEM_WB.readMemoryDataIn;
  EX_MEM.aluOutputOut >> MEM_WB.aluOutputIn;
  EX_MEM.writeRegisterOut >> MEM_WB.writeRegisterIn;

  EX_MEM.ctrlMemToRegOut >> MEM_WB.ctrlMemToRegIn;
  EX_MEM.ctrlRegWriteOut >> MEM_WB.ctrlRegWriteIn;

  // write-back stage
  MEM_WB.aluOutputOut >> writeBackSrcChooser.input0;
  MEM_WB.readMemoryDataOut >> writeBackSrcChooser.input1;
  MEM_WB.ctrlMemToRegOut >> writeBackSrcChooser.control;
}

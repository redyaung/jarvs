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
void MemoryUnit::operate() {
  if (ctrlMemRead.val) {
    Word readWord = memory.readBlock<1>(address.val)[0];
    readData << readWord;
  } else if (ctrlMemWrite.val) {
    memory.writeBlock(address.val, Block<1>{writeData.val});
  }
}

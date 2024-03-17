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

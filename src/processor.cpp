#include "processor.hpp"
#include "utils.hpp"
#include <stdexcept>
#include <ios>
#include <utility>

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
  bool isIType = (opcode == 0b0010011);
  bool isLw = (opcode == 0b0000011);
  bool isSw = (opcode == 0b0100011);
  bool isBeq = (opcode == 0b1100011);
  // for some reason, this kind of conversion may be necessary
  ctrlRegWrite << (isRType || isLw || isIType ? 0b1 : 0b0);
  ctrlAluSrc << (isLw || isSw || isIType ? 0b1 : 0b0);
  ctrlAluOp << (isBeq ? 0b01 : (isRType || isIType ? 0b10 : 0b00));
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
  readRegister1Out << readRegister1In.val;
  readRegister2Out << readRegister2In.val;
}

void EXMEMRegisters::operate() {
  ctrlMemWriteOut << 0;     // avoid unintentional writes

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
  ctrlRegWriteOut << 0;     // avoid unintentional writes

  readMemoryDataOut << readMemoryDataIn.val;
  aluOutputOut << aluOutputIn.val;

  ctrlMemToRegOut << ctrlMemToRegIn.val;
  writeRegisterOut << writeRegisterIn.val;
  // only assert the write control signal after other signals are properly set
  ctrlRegWriteOut << ctrlRegWriteIn.val;
}

// synchronize the internal signals connecting buffer and out
BufferedMEMWBRegisters::BufferedMEMWBRegisters() {
  buffer.readMemoryDataOut >> out.readMemoryDataIn;
  buffer.aluOutputOut >> out.aluOutputIn;

  buffer.ctrlMemToRegOut >> out.ctrlMemToRegIn;
  buffer.writeRegisterOut >> out.writeRegisterIn;
  buffer.ctrlRegWriteOut >> out.ctrlRegWriteIn;
}

// propagate changes from buffer to out
void BufferedMEMWBRegisters::bufferInputs() {
  buffer.operate();
}

// propagate changes from out
void BufferedMEMWBRegisters::operate() {
  out.operate();
}

void InstructionIssueUnit::operate() {
  pcOut << (shouldStall ? pcOut.val : pcIn.val);
}

ForwardingUnit::ForwardingUnit(
  IDEXRegisters &ID_EX,
  EXMEMRegisters &EX_MEM,
  BufferedMEMWBRegisters &MEM_WB
) : MEM_WB{&MEM_WB}, EX_MEM{&EX_MEM}, ID_EX{&ID_EX} {}

// the forwarding unit should also be called first among the in-sync units
void ForwardingUnit::operate() {
  // for each Rs1 and Rs2, stores a (register num, register data signal) pair
  const std::pair<int, InputSignal&> sourceRegisters[2] {
    { ID_EX->readRegister1In.val, ID_EX->readData1In },
    { ID_EX->readRegister2In.val, ID_EX->readData2In }
  };
  // handles forwarding from both EX/MEM and MEM/WB
  for (auto [registerNum, registerDataSignal] : sourceRegisters) {
    if (
      EX_MEM->ctrlRegWriteIn.val &&
      EX_MEM->writeRegisterIn.val != 0 &&
      EX_MEM->writeRegisterIn.val == registerNum
    ) {
      registerDataSignal.val = EX_MEM->aluOutputIn.val;
    } else if (
      MEM_WB->buffer.ctrlRegWriteIn.val &&
      MEM_WB->buffer.writeRegisterIn.val != 0 &&
      MEM_WB->buffer.writeRegisterIn.val == ID_EX->readRegister1In.val
    ) {
      Word val = MEM_WB->buffer.ctrlMemToRegIn.val ?
        MEM_WB->buffer.readMemoryDataIn.val : MEM_WB->buffer.aluOutputIn.val;
      registerDataSignal.val = val;
    }
  }
}

DataHazardDetectionUnit::DataHazardDetectionUnit(
  InstructionIssueUnit &issueUnit, IFIDRegisters &IF_ID, IDEXRegisters &ID_EX
) : issueUnit{&issueUnit}, IF_ID{&IF_ID}, ID_EX{&ID_EX} {}

void DataHazardDetectionUnit::operate() {
  uint32_t rs1 = extractBits(IF_ID->instructionIn.val, 15, 19);
  uint32_t rs2 = extractBits(IF_ID->instructionIn.val, 20, 24);
  if (
    ID_EX->ctrlMemReadIn.val &&
    (rs1 == ID_EX->writeRegisterIn.val || rs2 == ID_EX->writeRegisterIn.val)
  ) {
    // stall the pipeline by one cycle by
    //  1. deasserting MemWrite, RegWrite and Branch signals of instruction in IF
    //      - we effectively achieve this by zeroing out the instruction itself
    //  2. setting the PC to be equal to the latest instruction (in IF)
    IF_ID->instructionIn.val = 0x0;
    issueUnit->shouldStall = true;
  } else {
    issueUnit->shouldStall = false;
  }
}

PipelinedProcessor::PipelinedProcessor(bool useForwarding)
  : forwardingUnit(ID_EX, EX_MEM, MEM_WB),
    hazardDetectionUnit(issueUnit, IF_ID, ID_EX)
{
  synchronizeSignals();
  registerUnits(useForwarding);
}

// the order determines the order in which these in-sync units are called
void PipelinedProcessor::registerUnits(bool useForwarding) {
  // forwarding unit must be called before any other in-sync unit
  if (useForwarding) {
    syncedUnits.push_back(&hazardDetectionUnit);
    syncedUnits.push_back(&forwardingUnit);
  }

  syncedUnits.push_back(&EX_MEM);
  syncedUnits.push_back(&ID_EX);
  syncedUnits.push_back(&IF_ID);
  syncedUnits.push_back(&issueUnit);

  // must be called last as otherwise, there's no point in buffering
  syncedUnits.push_back(&MEM_WB);

  bufferedUnits.push_back(&MEM_WB);
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
  MEM_WB.out.writeRegisterOut >> registers.writeRegister;
  writeBackSrcChooser.output >> registers.writeData;
  MEM_WB.out.ctrlRegWriteOut >> registers.ctrlRegWrite;

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
  decoder.readRegister1 >> ID_EX.readRegister1In;
  decoder.readRegister2 >> ID_EX.readRegister2In;

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

  dataMemory.readData >> MEM_WB.buffer.readMemoryDataIn;
  EX_MEM.aluOutputOut >> MEM_WB.buffer.aluOutputIn;
  EX_MEM.writeRegisterOut >> MEM_WB.buffer.writeRegisterIn;

  EX_MEM.ctrlMemToRegOut >> MEM_WB.buffer.ctrlMemToRegIn;
  EX_MEM.ctrlRegWriteOut >> MEM_WB.buffer.ctrlRegWriteIn;

  // write-back stage
  MEM_WB.out.aluOutputOut >> writeBackSrcChooser.input0;
  MEM_WB.out.readMemoryDataOut >> writeBackSrcChooser.input1;
  MEM_WB.out.ctrlMemToRegOut >> writeBackSrcChooser.control;
}

std::ostream& operator<<(std::ostream& os, const InputSignal &input) {
  os << "input signal [val = " << std::dec << input.val;
  os << "(" << std::showbase << std::hex << input.val << std::dec << ")" << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os, const OutputSignal &output) {
  os << "output signal [val = " << std::dec << output.val;
  os << "(" << std::showbase << std::hex << output.val << std::dec << ")";
  os << ", #sync-ed inputs = " << output.syncedInputs.size() << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os, const DecodeUnit &decoder) {
  os << "in DecodeUnit: " << std::endl;
  os << "\t" << "instruction: " << decoder.instruction << std::endl;
  os << "\t" << "readRegister1: " << decoder.readRegister1 << std::endl;
  os << "\t" << "readRegister2: " << decoder.readRegister2 << std::endl;
  os << "\t" << "writeRegister: " << decoder.writeRegister << std::endl;
  os << "\t" << "func3: " << decoder.func3 << std::endl;
  os << "\t" << "func7: " << decoder.func7;
  return os;
}

std::ostream& operator<<(std::ostream& os, const ControlUnit &control) {
  os << "in ControlUnit: " << std::endl;
  os << "\t" << "instruction: " << control.instruction << std::endl;
  os << "\t" << "ctrlRegWrite: " << control.ctrlRegWrite << std::endl;
  os << "\t" << "ctrlAluSrc: " << control.ctrlAluSrc << std::endl;
  os << "\t" << "ctrlAluOp: " << control.ctrlAluOp << std::endl;
  os << "\t" << "ctrlBranch: " << control.ctrlBranch << std::endl;
  os << "\t" << "ctrlMemWrite: " << control.ctrlMemWrite << std::endl;
  os << "\t" << "ctrlMemRead: " << control.ctrlMemRead << std::endl;
  os << "\t" << "ctrlMemToReg: " << control.ctrlMemToReg;
  return os;
}

std::ostream& operator<<(std::ostream& os, const RegisterFileUnit &registers) {
  os << "in RegisterFileUnit: " << std::endl;
  os << "\t" << "readRegister1: " << registers.readRegister1 << std::endl;
  os << "\t" << "readRegister2: " << registers.readRegister2 << std::endl;
  os << "\t" << "writeRegister: " << registers.writeRegister << std::endl;
  os << "\t" << "writeData: " << registers.writeData << std::endl;
  os << "\t" << "ctrlRegWrite: " << registers.ctrlRegWrite << std::endl;
  os << "\t" << "readData1: " << registers.readData1 << std::endl;
  os << "\t" << "readData2: " << registers.readData2;
  return os;
}

std::ostream& operator<<(std::ostream& os, const ImmediateGenerator &immGen) {
  os << "in ImmediateGenerator: " << std::endl;
  os << "\t" << "instruction: " << immGen.instruction << std::endl;
  os << "\t" << "immediate: " << immGen.immediate;
  return os;
}

std::ostream& operator<<(std::ostream& os, const Multiplexer &mux) {
  os << "in Multiplexer: " << std::endl;
  os << "\t" << "input0: " << mux.input0 << std::endl;
  os << "\t" << "input1: " << mux.input1 << std::endl;
  os << "\t" << "control: " << mux.control << std::endl;
  os << "\t" << "output: " << mux.output;
  return os;
}

std::ostream& operator<<(std::ostream& os, const ALUControl &aluControl) {
  os << "in ALUControl: " << std::endl;
  os << "\t" << "instruction: " << aluControl.instruction << std::endl;
  os << "\t" << "ctrlAluOp: " << aluControl.ctrlAluOp << std::endl;
  os << "\t" << "aluOp: " << aluControl.aluOp;
  return os;
}

std::ostream& operator<<(std::ostream& os, const ALUUnit &alu) {
  os << "in ALUUnit: " << std::endl;
  os << "\t" << "input0: " << alu.input0 << std::endl;
  os << "\t" << "input1: " << alu.input1 << std::endl;
  os << "\t" << "aluOp: " << alu.aluOp << std::endl;
  os << "\t" << "output: " << alu.output << std::endl;
  os << "\t" << "zero: " << alu.zero;
  return os;
}

std::ostream& operator<<(std::ostream& os, const DataMemoryUnit &dataMemory) {
  os << "in DataMemoryUnit: " << std::endl;
  os << "\t" << "address: " << dataMemory.address << std::endl;
  os << "\t" << "writeData: " << dataMemory.writeData << std::endl;
  os << "\t" << "ctrlMemRead: " << dataMemory.ctrlMemRead << std::endl;
  os << "\t" << "ctrlMemWrite: " << dataMemory.ctrlMemWrite << std::endl;
  os << "\t" << "readData: " << dataMemory.readData;
  return os;
}

std::ostream& operator<<(std::ostream& os, const InstructionMemoryUnit &instructionMemory) {
  os << "in InstructionMemoryUnit: " << std::endl;
  os << "\t" << "address: " << instructionMemory.address << std::endl;
  os << "\t" << "instruction: " << instructionMemory.instruction;
  return os;
}

std::ostream& operator<<(std::ostream& os, const AndGate &gate) {
  os << "in AndGate: " << std::endl;
  os << "\t" << "input0: " << gate.input0 << std::endl;
  os << "\t" << "input1: " << gate.input1 << std::endl;
  os << "\t" << "output: " << gate.output;
  return os;
}

std::ostream& operator<<(std::ostream& os, const IFIDRegisters &IF_ID) {
  os << "in IFIDRegisters: " << std::endl;
  // os << "\t" << "pcIn: " << IF_ID.pcIn << std::endl;
  // os << "\t" << "instructionIn: " << IF_ID.instructionIn << std::endl;
  os << "\t" << "pcOut: " << IF_ID.pcOut << std::endl;
  os << "\t" << "instructionOut: " << IF_ID.instructionOut;
  return os;
}

// ignore the In signals in pipeline registers (for pretty-printing)
std::ostream& operator<<(std::ostream& os, const IDEXRegisters &ID_EX) {
  os << "in IDEXRegisters: " << std::endl;
  os << "\t" << "readData1Out: " << ID_EX.readData1Out << std::endl;
  os << "\t" << "readData2Out: " << ID_EX.readData2Out << std::endl;
  os << "\t" << "immediateOut: " << ID_EX.immediateOut << std::endl;
  os << "\t" << "instructionOut: " << ID_EX.instructionOut << std::endl;
  os << "\t" << "ctrlAluSrcOut: " << ID_EX.ctrlAluSrcOut << std::endl;
  os << "\t" << "ctrlAluOpOut: " << ID_EX.ctrlAluOpOut << std::endl;
  os << "\t" << "ctrlBranchOut: " << ID_EX.ctrlBranchOut << std::endl;
  os << "\t" << "ctrlMemWriteOut: " << ID_EX.ctrlMemWriteOut << std::endl;
  os << "\t" << "ctrlMemReadOut: " << ID_EX.ctrlMemReadOut << std::endl;
  os << "\t" << "ctrlMemToRegOut: " << ID_EX.ctrlMemToRegOut << std::endl;
  os << "\t" << "ctrlRegWriteOut: " << ID_EX.ctrlRegWriteOut << std::endl;
  os << "\t" << "writeRegisterOut: " << ID_EX.writeRegisterOut << std::endl;
  os << "\t" << "pcOut: " << ID_EX.pcOut;
  return os;
}

std::ostream& operator<<(std::ostream& os, const EXMEMRegisters &EX_MEM) {
  os << "in EXMEMRegisters: " << std::endl;
  os << "\t" << "branchAdderOutputOut: " << EX_MEM.branchAdderOutputOut << std::endl;
  os << "\t" << "zeroOut: " << EX_MEM.zeroOut << std::endl;
  os << "\t" << "aluOutputOut: " << EX_MEM.aluOutputOut << std::endl;
  os << "\t" << "readData2Out: " << EX_MEM.readData2Out << std::endl;
  os << "\t" << "ctrlBranchOut: " << EX_MEM.ctrlBranchOut << std::endl;
  os << "\t" << "ctrlMemWriteOut: " << EX_MEM.ctrlMemWriteOut << std::endl;
  os << "\t" << "ctrlMemReadOut: " << EX_MEM.ctrlMemReadOut << std::endl;
  os << "\t" << "ctrlMemToRegOut: " << EX_MEM.ctrlMemToRegOut << std::endl;
  os << "\t" << "ctrlRegWriteOut: " << EX_MEM.ctrlRegWriteOut << std::endl;
  os << "\t" << "writeRegisterOut: " << EX_MEM.writeRegisterOut;
  return os;
}

std::ostream& operator<<(std::ostream& os, const MEMWBRegisters &MEM_WB) {
  os << "in MEMWBRegisters: " << std::endl;
  os << "\t" << "readMemoryDataOut: " << MEM_WB.readMemoryDataOut << std::endl;
  os << "\t" << "aluOutputOut: " << MEM_WB.aluOutputOut << std::endl;
  os << "\t" << "ctrlMemToRegOut: " << MEM_WB.ctrlMemToRegOut << std::endl;
  os << "\t" << "ctrlRegWriteOut: " << MEM_WB.ctrlRegWriteOut << std::endl;
  os << "\t" << "writeRegisterOut: " << MEM_WB.writeRegisterOut;
  return os;
}

std::ostream& operator<<(std::ostream& os, const BufferedMEMWBRegisters &MEM_WB) {
  os << "in BufferedMEMWBRegisters: " << std::endl;
  os << "\t" << "buffer: " << MEM_WB.buffer << std::endl;
  os << "\t" << "out: " << MEM_WB.out;
  return os;
}

std::ostream& operator<<(std::ostream& os, const InstructionIssueUnit &issueUnit) {
  os << "in InstructionIssueUnit: " << std::endl;
  os << "\t" << "pcOut: " << issueUnit.pcOut;
  os << "\t" << "shouldStall: " << std::boolalpha << issueUnit.shouldStall;
  return os;
}

std::ostream& operator<<(std::ostream& os, const PipelinedProcessor &processor) {
  os << "pipelined processor" << std::endl;
  os << "\t" << "clock cycle = " << processor.clockCycle << std::endl;

  os << "pcChooser: " << processor.pcChooser << std::endl;
  os << "issueUnit: " << processor.issueUnit << std::endl;
  os << "pcAdder: " << processor.pcAdder << std::endl;
  os << "instructionMemory: " << processor.instructionMemory << std::endl;

  os << "IF_ID: " << processor.IF_ID << std::endl;
  os << "decoder: " << processor.decoder << std::endl;
  os << "control: " << processor.control << std::endl;
  os << "registers: " << processor.registers << std::endl;
  os << "immGen: " << processor.immGen << std::endl;

  os << "ID_EX: " << processor.ID_EX << std::endl;
  os << "branchAdder: " << processor.branchAdder << std::endl;
  os << "aluSrc2Chooser: " << processor.aluSrc2Chooser << std::endl;
  os << "alu: " << processor.alu << std::endl;
  os << "aluControl: " << processor.aluControl << std::endl;

  os << "EX_MEM: " << processor.EX_MEM << std::endl;
  os << "branchChooser: " << processor.branchChooser << std::endl;
  os << "dataMemory: " << processor.dataMemory << std::endl;

  os << "MEM_WB: " << processor.MEM_WB << std::endl;
  os << "writeBackSrcChooser: " << processor.writeBackSrcChooser;
  return os;
}

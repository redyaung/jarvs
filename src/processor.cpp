#include "processor.hpp"
#include "utils.hpp"
#include <initializer_list>
#include <stdexcept>
#include <ios>
#include <utility>
#include <optional>

namespace __ProcessorUtils {
  // corresponds to the four instruction types we defined in assembler
  enum class InstructionFmt {
    R, I, S, SB, U, UJ
  };

  // omitted: auipc(U), ecall(I), ebreak(I)
  constexpr uint32_t RFmtOpcode = 0b0110011;
  constexpr uint32_t aluIFmtOpcode = 0b0010011;
  constexpr uint32_t loadIFmtOpcode = 0b0000011;
  constexpr uint32_t jalrIFmtOpcode = 0b1100111;  // ONLY jalr
  constexpr uint32_t SFmtOpcode = 0b0100011;      // stores
  constexpr uint32_t SBFmtOpcode = 0b1100011;     // conditional branches
  constexpr uint32_t UFmtOpcode = 0b0110111;      // ONLY lui
  constexpr uint32_t UJFmtOpcode = 0b1101111;     // ONLY jal

  constexpr InstructionFmt fmt(uint32_t opcode) {
    switch (opcode) {
      case RFmtOpcode:
        return InstructionFmt::R;
      case aluIFmtOpcode:
      case loadIFmtOpcode:
      case jalrIFmtOpcode:
        return InstructionFmt::I;
      case SFmtOpcode:
        return InstructionFmt::S;
      case SBFmtOpcode:
        return InstructionFmt::SB;
      case UFmtOpcode:
        return InstructionFmt::U;
      case UJFmtOpcode:
        return InstructionFmt::UJ;
      default:
        throw std::invalid_argument("invalid opcode: " + std::to_string(opcode));
    }
  }

  constexpr inline uint32_t opcode(Word instruction) {
    return extractBits(instruction, 0, 6);
  }
  constexpr inline uint32_t funct3(Word instruction) {
    return extractBits(instruction, 12, 14);
  }
  constexpr inline uint32_t funct7(Word instruction) {
    return extractBits(instruction, 25, 31);
  }
  constexpr inline uint32_t rs1(Word instruction) {
    return extractBits(instruction, 15, 19);
  }
  constexpr inline uint32_t rs2(Word instruction) {
    return extractBits(instruction, 20, 24);
  }

  constexpr inline bool isNop(Word instruction) {
    return instruction == 0x0;
  }

  // see https://graphics.stanford.edu/~seander/bithacks.html#FixedSignExtend
  template<unsigned B>
  constexpr inline int32_t signExtend(int32_t x) {
    struct { int32_t x: B; } s;
    return s.x = x;
  }
}

using namespace __ProcessorUtils;

Word Signal::operator*() const {
  return val;
}

Word &Signal::operator*() {
  return val;
}

InputSignal::InputSignal(Unit *unit) : unit{unit}, Signal{} {}

void InputSignal::changeValue(Word newValue) {
  val = newValue;
  unit->notifyInputChange();
}

OutputSignal& OutputSignal::operator>>(InputSignal &signal) {
  syncedInputs.push_back(&signal);
  return *this;
}

void OutputSignal::operator<<(Word newValue) {
  val = newValue;
  for (InputSignal *signal : syncedInputs) {
    signal->changeValue(newValue);
  }
}

Word Flushable::zeroOnFlush(Word val) const {
  return *shouldFlush ? Word(0u) : val;
}

void DecodeUnit::operate() {
  readRegister1 << extractBits(*instruction, 15, 19);
  readRegister2 << extractBits(*instruction, 20, 24);
  writeRegister << extractBits(*instruction, 7, 11);
  func3 << extractBits(*instruction, 12, 14);
  func7 << extractBits(*instruction, 25, 31);
}

ControlUnit::ControlUnit(RegisterFile<RegisterType::Integer> *intRegs)
  : intRegs{intRegs} {}

// see Patterson-Hennessy fig 4.26 (pg 281)
void ControlUnit::operate() {
  // zero out control signals on a nop instruction
  if (isNop(*instruction)) {
    ctrlRegWrite << 0u;
    ctrlMemWrite << 0u;
    ctrlMemRead << 0u;
    ctrlBranch << 0u;
    ctrlUseRegBase << 0u;
    ctrlIsJump << 0u;
    return;
  }

  uint32_t _opcode = opcode(*instruction);
  InstructionFmt _fmt = fmt(_opcode);
  // this entire list seems like a load of crap
  bool isR = (_fmt == InstructionFmt::R);
  bool isAluI = (_opcode == aluIFmtOpcode);
  bool isLoad = (_opcode == loadIFmtOpcode);
  bool isStore = (_fmt == InstructionFmt::S);
  bool isCondBranch = (_fmt == InstructionFmt::SB);
  bool isJalr = (_opcode == jalrIFmtOpcode);
  bool isJal = (_fmt == InstructionFmt::UJ);
  bool isJump = isJalr || isJal;

  ctrlRegWrite << (isR || isLoad || isAluI);
  ctrlAluSrc << (isLoad || isStore || isAluI);
  ctrlAluOp << (isCondBranch ? 0b01 : (isR || isAluI ? 0b10 : 0b00));
  ctrlMemWrite << isStore;
  ctrlMemRead << isLoad;
  ctrlMemToReg << isLoad;
  // branching logic (conditional + unconditional)
  ctrlBranch << isCondBranch;
  ctrlUseRegBase << isJalr;
  ctrlIsJump << isJump;

  // link the return address register here (the linking happens *immediately*)
  if (isJump && *writeRegister != 0) {
    intRegs->writeRegister(*writeRegister, *pc + 4);
  }
}

void RegisterFileUnit::operate() {
  // recall: when writes and reads are in the same cycle, writes complete first
  if (*ctrlRegWrite) {
    intRegs.writeRegister(*writeRegister, *writeData);
  }
  readData1 << intRegs.readRegister(*readRegister1);
  readData2 << intRegs.readRegister(*readRegister2);
}

// issues:
//  - the imm field for branches (SB & UJ) treat imm bits as byte offsets
//    - in a real datapath, they are instead treated as halfword offsets
//    - a fix is simple (x2) but the current version is easier to reason in assembly
void ImmediateGenerator::operate() {
  // ignore nop instructions
  if (isNop(*instruction)) {
    return;
  }
  InstructionFmt format = fmt(opcode(*instruction));
  switch (format) {
    case InstructionFmt::I: {   // alu, loads, jalr
      uint32_t rawBits = extractBits(*instruction, 20, 31);
      immediate << int32_t(signExtend<12>(rawBits));
      break;
    }
    case InstructionFmt::S:     // stores
    case InstructionFmt::SB: {  // conditional branches
      uint32_t immLow = extractBits(*instruction, 7, 11);
      uint32_t immHigh = extractBits(*instruction, 25, 31);
      uint32_t rawBits = (immHigh<< 5) + immLow;
      immediate << int32_t(signExtend<12>(rawBits));
      break;
    }
    case InstructionFmt::U: {   // lui
      uint32_t imm = extractBits(*instruction, 12, 31);
      immediate << int32_t(imm<< 12);
      break;
    }
    case InstructionFmt::UJ: {  // jal
      uint32_t rawBits = extractBits(*instruction, 12, 31);
      immediate << int32_t(signExtend<20>(rawBits));
      break;
    }
    default:
      break;    // ignore R types
  }
}

void Multiplexer::operate() {
  output << (*control == 0u ? *input0 : *input1);
}

// refer to Patterson-Hennessy fig 4.12 (pg 270)
void ALUControl::operate() {
  assert(contains(std::array{0b00, 0b01, 0b10}, *ctrlAluOp));
  if (*ctrlAluOp == 0b00u) {         // loads and stores
    aluOp << uint32_t(ALUOp::Add);
    return;
  }
  if (*ctrlAluOp == 0b01u) {         // conditional branches (currently only beq)
    aluOp << uint32_t(ALUOp::Sub);
    return;
  }
  // R-format and ALU I-format instructions
  switch (funct3(*instruction)) {
    case 0x0: {   // add or sub
      switch (funct7(*instruction)) {
        case 0x00:    // add
          aluOp << uint32_t(ALUOp::Add);
          break;
        case 0x20:    // sub
          aluOp << uint32_t(ALUOp::Sub);
          break;
      }
      break;
    }
    case 0x6:   // or
      aluOp << uint32_t(ALUOp::Or);
      break;
    case 0x7:   // and
      aluOp << uint32_t(ALUOp::And);
      break;
    case 0x1:   // sll
      aluOp << uint32_t(ALUOp::Sll);
      break;
    case 0x5:   // srl
      aluOp << uint32_t(ALUOp::Srl);
      break;
  }
}

void BranchALUControl::operate() {
  switch (*func3) {
    case 0x0:     // beq
      branchAluOp << uint32_t(BranchALUOp::Eq);
      break;
    case 0x1:     // bne
      branchAluOp << uint32_t(BranchALUOp::Ne);
      break;
    case 0x4:     // blt
      branchAluOp << uint32_t(BranchALUOp::Lt);
      break;
    case 0x5:     // bge
      branchAluOp << uint32_t(BranchALUOp::Ge);
      break;
  }
}

void ALUUnit::operate() {
  switch (ALUOp{uint32_t(*aluOp)}) {
    case ALUOp::Add:
      output << (int32_t(*input0) + int32_t(*input1));
      break;
    case ALUOp::Sub:
      output << (int32_t(*input0) - int32_t(*input1));
      break;
    case ALUOp::And:
      output << (int32_t(*input0) & int32_t(*input1));
      break;
    case ALUOp::Or:
      output << (int32_t(*input0) | int32_t(*input1));
      break;
    case ALUOp::Sll:
      output << (int32_t(*input0)<< int32_t(*input1));
      break;
    case ALUOp::Srl:
      output << (int32_t(*input0)>> int32_t(*input1));
      break;
  }
  zero << (*output == 0);
}

void BranchALUUnit::operate() {
  switch (BranchALUOp{uint32_t(*branchAluOp)}) {
    case BranchALUOp::Eq:
      output << (*input0 == *input1);
      break;
    case BranchALUOp::Ne:
      output << (*input0 != *input1);
      break;
    case BranchALUOp::Lt:
      output << (*input0 < *input1);
      break;
    case BranchALUOp::Ge:
      output << (*input0 >= *input1);
      break;
  }
}

DataMemoryUnit::DataMemoryUnit(std::shared_ptr<TimedMemory> memory)
  : memory{memory} {}

void DataMemoryUnit::operate() {
  if (*ctrlMemRead) {
    std::optional<Block> readVal = memory->readBlock(*address, 1);
    isReady << readVal.has_value();
    if (readVal.has_value()) {
      readData << readVal.value()[0];
    }
  } else if (*ctrlMemWrite) {
    isReady << memory->writeBlock(*address, Block{*writeData});
  } else {
    assert(memory->getState() == MemoryState::Ready);
    isReady << true;
  }
}

void InstructionMemoryUnit::operate() {
  instruction << memory.readBlock(*address, 1).value()[0];
}

void AndGate::operate() {
  output << (*input0 && *input1);
}

void OrGate::operate() {
  output << (*input0 || *input1);
}

// note: it turns out zeroing out *instructionIn directly on shouldFlush can lead to problems
// if the output signal connected to instructionIn is not refreshed on the next turn!
//
// there are 2 ways to avoid this:
//  i. propagate signals even if they are unchanged
//  ii. don't directly set signal values
void IFIDRegisters::operate() {
  if (*shouldFreeze) {
    return;
  }

  instructionOut << zeroOnFlush(*instructionIn);
  pcOut << *pcIn;
}

void IDEXRegisters::operate() {
  if (*shouldFreeze) {
    return;
  }

  readData1Out << *readData1In;
  readData2Out << *readData2In;
  immediateOut << *immediateIn;
  instructionOut << *instructionIn;

  ctrlAluSrcOut << *ctrlAluSrcIn;
  ctrlAluOpOut << *ctrlAluOpIn;
  ctrlMemWriteOut << zeroOnFlush(*ctrlMemWriteIn);
  ctrlMemReadOut << zeroOnFlush(*ctrlMemReadIn);
  ctrlMemToRegOut << *ctrlMemToRegIn;
  ctrlRegWriteOut << zeroOnFlush(*ctrlRegWriteIn);
  writeRegisterOut << *writeRegisterIn;
  readRegister1Out << *readRegister1In;
  readRegister2Out << *readRegister2In;

  pcOut << *pcIn;
}

void EXMEMRegisters::operate() {
  if (*shouldFreeze) {
    return;
  }

  ctrlMemWriteOut << 0;     // avoid unintentional writes
  ctrlMemReadOut << 0;      // avoid unintentional reads

  zeroOut << *zeroIn;
  aluOutputOut << *aluOutputIn;
  readData2Out << *readData2In;

  ctrlMemWriteOut << zeroOnFlush(*ctrlMemWriteIn);
  ctrlMemReadOut << zeroOnFlush(*ctrlMemReadIn);
  ctrlMemToRegOut << *ctrlMemToRegIn;
  ctrlRegWriteOut << zeroOnFlush(*ctrlRegWriteIn);
  writeRegisterOut << *writeRegisterIn;

  pcOut << *pcIn;
  instructionOut << *instructionIn;
}

void MEMWBRegisters::operate() {
  ctrlRegWriteOut << 0;     // avoid unintentional writes

  readMemoryDataOut << *readMemoryDataIn;
  aluOutputOut << *aluOutputIn;

  ctrlMemToRegOut << *ctrlMemToRegIn;
  writeRegisterOut << *writeRegisterIn;
  // only assert the write control signal after other signals are properly set
  ctrlRegWriteOut << zeroOnFlush(*ctrlRegWriteIn);

  pcOut << *pcIn;
  instructionOut << *instructionIn;
}

void InstructionIssueUnit::operate() {
  if (*shouldFreeze) {
    return;
  }
  pcOut << *pcIn;
}

// synchronize signals in the c-tor
BufferedMEMWBRegisters::BufferedMEMWBRegisters() {
  buffer.readMemoryDataOut >> out.readMemoryDataIn;
  buffer.aluOutputOut >> out.aluOutputIn;

  buffer.ctrlMemToRegOut >> out.ctrlMemToRegIn;
  buffer.writeRegisterOut >> out.writeRegisterIn;
  buffer.ctrlRegWriteOut >> out.ctrlRegWriteIn;

  buffer.pcOut >> out.pcIn;
  buffer.instructionOut >> out.instructionIn;
}

BufferedInstructionIssueUnit::BufferedInstructionIssueUnit() {
  buffer.pcOut >> out.pcIn;
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
    { *ID_EX->readRegister1In, ID_EX->readData1In },
    { *ID_EX->readRegister2In, ID_EX->readData2In }
  };
  // handles forwarding from both EX/MEM and MEM/WB
  for (auto [registerNum, registerDataSignal] : sourceRegisters) {
    if (
      *EX_MEM->ctrlRegWriteIn &&
      *EX_MEM->writeRegisterIn != 0 &&
      *EX_MEM->writeRegisterIn == registerNum
    ) {
      *registerDataSignal = *EX_MEM->aluOutputIn;
    } else if (
      *MEM_WB->buffer.ctrlRegWriteIn &&
      *MEM_WB->buffer.writeRegisterIn != 0 &&
      *MEM_WB->buffer.writeRegisterIn == registerNum
    ) {
      Word val = *MEM_WB->buffer.ctrlMemToRegIn ?
        *MEM_WB->buffer.readMemoryDataIn : *MEM_WB->buffer.aluOutputIn;
      *registerDataSignal = val;
    }
  }
}

void MemoryHazardDetectionUnit::operate() {
  // if the data memory is still operating, freeze all pipeline registers incl. the issue
  // unit up to EX-MEM and flush the MEM-WB pipeline registers
  bool isBusy = !*isDataMemoryReady;
  shouldFreezeIssue << isBusy;
  shouldFreezeIF_ID << isBusy;
  shouldFreezeID_EX << isBusy;
  shouldFreezeEX_MEM << isBusy;
  shouldFlushMEM_WB << isBusy;
}

DataHazardDetectionUnit::DataHazardDetectionUnit(
  bool isForwarding,
  BufferedInstructionIssueUnit &issueUnit,
  IFIDRegisters &IF_ID,
  IDEXRegisters &ID_EX,
  EXMEMRegisters &EX_MEM
) : isForwarding{isForwarding}, issueUnit{&issueUnit}, IF_ID{&IF_ID}, ID_EX{&ID_EX},
  EX_MEM{&EX_MEM} {}

void DataHazardDetectionUnit::operate() {
  auto instruction = *IF_ID->instructionIn;
  bool shouldStall = hasDataHazard(rs1(instruction), rs2(instruction));
  shouldFreezeIssue << shouldStall;
  shouldFlushIF_ID << shouldStall;
}

bool DataHazardDetectionUnit::hasDataHazard(uint32_t rs1, uint32_t rs2) {
  // forwarding: only check for load-use data hazard
  if (isForwarding) {
    return (
      *ID_EX->ctrlMemReadIn &&
      (rs1 == *ID_EX->writeRegisterIn || rs2 == *ID_EX->writeRegisterIn)
    );
  }
  // no forwarding: check for all data hazards
  return (
    (
      *ID_EX->ctrlRegWriteIn && *ID_EX->writeRegisterIn != 0 &&
      (rs1 == *ID_EX->writeRegisterIn || rs2 == *ID_EX->writeRegisterIn)
    ) ||
    (
      *EX_MEM->ctrlRegWriteIn && *EX_MEM->writeRegisterIn != 0 &&
      (rs1 == *EX_MEM->writeRegisterIn || rs2 == *EX_MEM->writeRegisterIn)
    )
  );
}

PipelinedProcessor::PipelinedProcessor(
  bool useForwarding,
  size_t memoryLatency
) : dataMemory(std::shared_ptr<TimedMemory>(
      new TimedMainMemory(8, memoryLatency)
    )),
    forwardingUnit(ID_EX, EX_MEM, MEM_WB),
    hazardDetectionUnit(useForwarding, issueUnit, IF_ID, ID_EX, EX_MEM),
    control(&registers.intRegs)
{
  synchronizeSignals();
  registerUnits(useForwarding);
}

// the order determines the order in which these in-sync units are called
void PipelinedProcessor::registerUnits(bool useForwarding) {
  // priority unit should generally not be registered into syncedUnits like bufferedUnits

  syncedUnits.push_back(&EX_MEM);
  syncedUnits.push_back(&dataMemory);
  syncedUnits.push_back(&ID_EX);
  syncedUnits.push_back(&IF_ID);

  // buffered units should be called last (otherwise, no point in buffering)
  syncedUnits.push_back(&issueUnit);
  syncedUnits.push_back(&MEM_WB);

  syncedUnits.push_back(&hazardDetectionUnit);
  syncedUnits.push_back(&memHazardUnit);
  if (useForwarding) {
    syncedUnits.push_back(&forwardingUnit);
  }

  bufferedUnits.push_back(&MEM_WB);
  bufferedUnits.push_back(&issueUnit);
}

void PipelinedProcessor::synchronizeSignals() {
  // fetch stage
  pcAdder.output >> pcChooser.input0;
  branchAddrAlu.output >> pcChooser.input1;
  branchDecisionMaker.output >> pcChooser.control;

  memHazardUnit.shouldFreezeIssue >> issueUnitFreezeDecisionMaker.input0;
  hazardDetectionUnit.shouldFreezeIssue >> issueUnitFreezeDecisionMaker.input1;

  pcChooser.output >> issueUnit.buffer.pcIn;
  issueUnitFreezeDecisionMaker.output >> issueUnit.buffer.shouldFreeze;

  issueUnit.out.pcOut >> pcAdder.input0;
  *pcAdder.input1 = 4u;    // hardcoded

  issueUnit.out.pcOut >> instructionMemory.address;

  hazardDetectionUnit.shouldFlushIF_ID >> IF_ID_flushDecisionMaker.input0;
  branchDecisionMaker.output >> IF_ID_flushDecisionMaker.input1;

  issueUnit.out.pcOut >> IF_ID.pcIn;
  instructionMemory.instruction >> IF_ID.instructionIn;
  IF_ID_flushDecisionMaker.output >> IF_ID.shouldFlush;
  memHazardUnit.shouldFreezeIF_ID >> IF_ID.shouldFreeze;

  // decode stage
  IF_ID.instructionOut >> decoder.instruction;

  IF_ID.instructionOut >> control.instruction;
  IF_ID.pcOut >> control.pc;
  decoder.writeRegister >> control.writeRegister;

  decoder.readRegister1 >> registers.readRegister1;
  decoder.readRegister2 >> registers.readRegister2;
  MEM_WB.out.writeRegisterOut >> registers.writeRegister;
  writeBackSrcChooser.output >> registers.writeData;
  MEM_WB.out.ctrlRegWriteOut >> registers.ctrlRegWrite;

  IF_ID.instructionOut >> immGen.instruction;

  IF_ID.pcOut >> branchAddrChooser.input0;
  registers.readData1 >> branchAddrChooser.input1;
  control.ctrlUseRegBase >> branchAddrChooser.control;

  branchAddrChooser.output >> branchAddrAlu.input0;
  immGen.immediate >> branchAddrAlu.input1;

  decoder.func3 >> branchDecisionAluControl.func3;

  registers.readData1 >> branchDecisionAlu.input0;
  registers.readData2 >> branchDecisionAlu.input1;
  branchDecisionAluControl.branchAluOp >> branchDecisionAlu.branchAluOp;

  control.ctrlBranch >> condBranchDecisionMaker.input0;
  branchDecisionAlu.output >> condBranchDecisionMaker.input1;

  condBranchDecisionMaker.output >> branchDecisionMaker.input0;
  control.ctrlIsJump >> branchDecisionMaker.input1;

  control.ctrlAluOp >> ID_EX.ctrlAluOpIn;
  control.ctrlAluSrc >> ID_EX.ctrlAluSrcIn;
  control.ctrlMemWrite >> ID_EX.ctrlMemWriteIn;
  control.ctrlMemRead >> ID_EX.ctrlMemReadIn;
  control.ctrlMemWrite >> ID_EX.ctrlMemWriteIn;
  control.ctrlMemToReg >> ID_EX.ctrlMemToRegIn;
  control.ctrlRegWrite >> ID_EX.ctrlRegWriteIn;

  registers.readData1 >> ID_EX.readData1In;
  registers.readData2 >> ID_EX.readData2In;
  immGen.immediate >> ID_EX.immediateIn;
  IF_ID.instructionOut >> ID_EX.instructionIn;
  decoder.writeRegister >> ID_EX.writeRegisterIn;
  decoder.readRegister1 >> ID_EX.readRegister1In;
  decoder.readRegister2 >> ID_EX.readRegister2In;

  IF_ID.pcOut >> ID_EX.pcIn;

  memHazardUnit.shouldFreezeID_EX >> ID_EX.shouldFreeze;

  // execute stage
  ID_EX.readData2Out >> aluSrc2Chooser.input0;
  ID_EX.immediateOut >> aluSrc2Chooser.input1;
  ID_EX.ctrlAluSrcOut >> aluSrc2Chooser.control;

  ID_EX.readData1Out >> alu.input0;
  aluSrc2Chooser.output >> alu.input1;
  aluControl.aluOp >> alu.aluOp;

  ID_EX.instructionOut >> aluControl.instruction;
  ID_EX.ctrlAluOpOut >> aluControl.ctrlAluOp;

  ID_EX.ctrlMemWriteOut >> EX_MEM.ctrlMemWriteIn;
  ID_EX.ctrlMemReadOut >> EX_MEM.ctrlMemReadIn;
  ID_EX.ctrlMemToRegOut >> EX_MEM.ctrlMemToRegIn;
  ID_EX.ctrlRegWriteOut >> EX_MEM.ctrlRegWriteIn;
  ID_EX.writeRegisterOut >> EX_MEM.writeRegisterIn;

  alu.zero >> EX_MEM.zeroIn;
  alu.output >> EX_MEM.aluOutputIn;
  ID_EX.readData2Out >> EX_MEM.readData2In;

  ID_EX.pcOut >> EX_MEM.pcIn;
  ID_EX.instructionOut >> EX_MEM.instructionIn;

  memHazardUnit.shouldFreezeEX_MEM >> EX_MEM.shouldFreeze;

  // memory stage
  EX_MEM.aluOutputOut >> dataMemory.address;
  EX_MEM.readData2Out >> dataMemory.writeData;
  EX_MEM.ctrlMemWriteOut >> dataMemory.ctrlMemWrite;
  EX_MEM.ctrlMemReadOut >> dataMemory.ctrlMemRead;

  dataMemory.readData >> MEM_WB.buffer.readMemoryDataIn;
  EX_MEM.aluOutputOut >> MEM_WB.buffer.aluOutputIn;
  EX_MEM.writeRegisterOut >> MEM_WB.buffer.writeRegisterIn;

  EX_MEM.ctrlMemToRegOut >> MEM_WB.buffer.ctrlMemToRegIn;
  EX_MEM.ctrlRegWriteOut >> MEM_WB.buffer.ctrlRegWriteIn;

  EX_MEM.pcOut >> MEM_WB.buffer.pcIn;
  EX_MEM.instructionOut >> MEM_WB.buffer.instructionIn;
  memHazardUnit.shouldFlushMEM_WB >> MEM_WB.buffer.shouldFlush;

  // write-back stage
  MEM_WB.out.aluOutputOut >> writeBackSrcChooser.input0;
  MEM_WB.out.readMemoryDataOut >> writeBackSrcChooser.input1;
  MEM_WB.out.ctrlMemToRegOut >> writeBackSrcChooser.control;

  // miscellaneous units
  dataMemory.isReady >> memHazardUnit.isDataMemoryReady;
}

std::ostream& operator<<(std::ostream& os, const InputSignal &input) {
  os << std::dec << int32_t(*input) << " ";
  os << "(" << std::showbase << std::hex << int32_t(*input) << std::dec << ")";
  os << " [in]";
  return os;
}

std::ostream& operator<<(std::ostream& os, const OutputSignal &output) {
  os << std::dec << int32_t(*output) << " ";
  os << "(" << std::showbase << std::hex << int32_t(*output) << std::dec << ")";
  os << " [out]";
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
  os << "\t" << "ctrlMemWrite: " << control.ctrlMemWrite << std::endl;
  os << "\t" << "ctrlMemRead: " << control.ctrlMemRead << std::endl;
  os << "\t" << "ctrlMemToReg: " << control.ctrlMemToReg << std::endl;
  os << "\t" << "ctrlBranch: " << control.ctrlBranch << std::endl;
  os << "\t" << "ctrlUseRegBase: " << control.ctrlUseRegBase << std::endl;
  os << "\t" << "ctrlIsJump: " << control.ctrlIsJump;
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

std::ostream& operator<<(std::ostream& os, const BranchALUControl &branchAluControl) {
  os << "in branchALUControl: " << std::endl;
  os << "\t" << "func3: " << branchAluControl.func3 << std::endl;
  os << "\t" << "branchAluOp: " << branchAluControl.branchAluOp;
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

std::ostream& operator<<(std::ostream& os, const BranchALUUnit &branchAlu) {
  os << "in BranchALUUnit: " << std::endl;
  os << "\t" << "input0: " << branchAlu.input0 << std::endl;
  os << "\t" << "input1: " << branchAlu.input1 << std::endl;
  os << "\t" << "branchAluOp: " << branchAlu.branchAluOp << std::endl;
  os << "\t" << "output: " << branchAlu.output;
  return os;
}

std::ostream& operator<<(std::ostream& os, const DataMemoryUnit &dataMemory) {
  os << "in DataMemoryUnit: " << std::endl;
  os << "\t" << "address: " << dataMemory.address << std::endl;
  os << "\t" << "writeData: " << dataMemory.writeData << std::endl;
  os << "\t" << "ctrlMemRead: " << dataMemory.ctrlMemRead << std::endl;
  os << "\t" << "ctrlMemWrite: " << dataMemory.ctrlMemWrite << std::endl;
  os << "\t" << "readData: " << dataMemory.readData;
  os << "\t" << "isReady: " << dataMemory.isReady;
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

std::ostream& operator<<(std::ostream& os, const OrGate &gate) {
  os << "in OrGate: " << std::endl;
  os << "\t" << "input0: " << gate.input0 << std::endl;
  os << "\t" << "input1: " << gate.input1 << std::endl;
  os << "\t" << "output: " << gate.output;
  return os;
}

std::ostream& operator<<(std::ostream& os, const IFIDRegisters &IF_ID) {
  os << "in IFIDRegisters: " << std::endl;
  os << "\t" << "shouldFreeze: " << IF_ID.shouldFreeze << std::endl;
  os << "\t" << "shouldFlush: " << IF_ID.shouldFlush << std::endl;
  os << "\t" << "pcOut: " << IF_ID.pcOut << std::endl;
  os << "\t" << "instructionOut: " << IF_ID.instructionOut;
  return os;
}

// ignore the In signals in pipeline registers (for pretty-printing)
std::ostream& operator<<(std::ostream& os, const IDEXRegisters &ID_EX) {
  os << "in IDEXRegisters: " << std::endl;
  os << "\t" << "shouldFreeze: " << ID_EX.shouldFreeze << std::endl;
  os << "\t" << "shouldFlush: " << ID_EX.shouldFlush << std::endl;
  os << "\t" << "readData1Out: " << ID_EX.readData1Out << std::endl;
  os << "\t" << "readData2Out: " << ID_EX.readData2Out << std::endl;
  os << "\t" << "immediateOut: " << ID_EX.immediateOut << std::endl;
  os << "\t" << "instructionOut: " << ID_EX.instructionOut << std::endl;
  os << "\t" << "ctrlAluSrcOut: " << ID_EX.ctrlAluSrcOut << std::endl;
  os << "\t" << "ctrlAluOpOut: " << ID_EX.ctrlAluOpOut << std::endl;
  os << "\t" << "ctrlMemWriteOut: " << ID_EX.ctrlMemWriteOut << std::endl;
  os << "\t" << "ctrlMemReadOut: " << ID_EX.ctrlMemReadOut << std::endl;
  os << "\t" << "ctrlMemToRegOut: " << ID_EX.ctrlMemToRegOut << std::endl;
  os << "\t" << "ctrlRegWriteOut: " << ID_EX.ctrlRegWriteOut << std::endl;
  os << "\t" << "writeRegisterOut: " << ID_EX.writeRegisterOut;
  return os;
}

std::ostream& operator<<(std::ostream& os, const EXMEMRegisters &EX_MEM) {
  os << "in EXMEMRegisters: " << std::endl;
  os << "\t" << "shouldFreeze: " << EX_MEM.shouldFreeze << std::endl;
  os << "\t" << "shouldFlush: " << EX_MEM.shouldFlush << std::endl;
  os << "\t" << "zeroOut: " << EX_MEM.zeroOut << std::endl;
  os << "\t" << "aluOutputOut: " << EX_MEM.aluOutputOut << std::endl;
  os << "\t" << "readData2Out: " << EX_MEM.readData2Out << std::endl;
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

std::ostream& operator<<(std::ostream& os, const InstructionIssueUnit &issueUnit) {
  os << "in InstructionIssueUnit: " << std::endl;
  os << "\t" << "shouldFreeze: " << issueUnit.shouldFreeze << std::endl;
  os << "\t" << "pcOut: " << issueUnit.pcOut;
  return os;
}

std::ostream& operator<<(std::ostream& os, const BufferedMEMWBRegisters &MEM_WB) {
  os << "in BufferedMEMWBRegisters: " << std::endl;
  os << "\t" << "buffer: " << MEM_WB.buffer << std::endl;
  os << "\t" << "out: " << MEM_WB.out;
  return os;
}

std::ostream& operator<<(std::ostream& os, const BufferedInstructionIssueUnit &issueUnit) {
  os << "in BufferedInstructionIssueUnit: " << std::endl;
  os << "\t" << "buffer: " << issueUnit.buffer << std::endl;
  os << "\t" << "out: " << issueUnit.out;
  return os;
}

std::ostream& operator<<(std::ostream& os, const DataHazardDetectionUnit &hazardUnit) {
  os << "in DataHazardDetectionUnit: " << std::endl;
  os << "\t" << "shouldFlushIF_ID: " << hazardUnit.shouldFlushIF_ID << std::endl;
  os << "\t" << "shouldFreezeIssue: " << hazardUnit.shouldFreezeIssue;
  return os;
}

std::ostream& operator<<(std::ostream& os, const MemoryHazardDetectionUnit &hazardUnit) {
  os << "in MemoryHazardDetectionUnit: " << std::endl;
  os << "\t" << "shouldFreezeIssue: " << hazardUnit.shouldFreezeIssue << std::endl;
  os << "\t" << "shouldFreezeIF_ID: " << hazardUnit.shouldFreezeIF_ID << std::endl;
  os << "\t" << "shouldFreezeID_EX: " << hazardUnit.shouldFreezeID_EX << std::endl;
  os << "\t" << "shouldFreezeEX_MEM: " << hazardUnit.shouldFreezeEX_MEM << std::endl;
  os << "\t" << "shouldFlushMEM_WB: " << hazardUnit.shouldFlushMEM_WB;
  return os;
}

std::ostream& operator<<(std::ostream& os, const PipelinedProcessor &processor) {
  os << "pipelined processor" << std::endl;
  os << "\t" << "clock cycle = " << processor.clockCycle << std::endl;
  os << std::string(32, '-') << std::endl;

  os << "dataHazardDetectionUnit: " << processor.hazardDetectionUnit << std::endl;
  os << "memHazardDetectionUnit: " << processor.memHazardUnit << std::endl;
  os << std::string(32, '-') << std::endl;

  os << "pcChooser: " << processor.pcChooser << std::endl;
  os << "issueUnitFreezeDecisionMaker: " << processor.issueUnitFreezeDecisionMaker << std::endl;
  os << "issueUnit: " << processor.issueUnit << std::endl;
  os << "pcAdder: " << processor.pcAdder << std::endl;
  os << "instructionMemory: " << processor.instructionMemory << std::endl;
  os << "IF_ID_flushDecisionMaker: " << processor.IF_ID_flushDecisionMaker << std::endl;
  os << "IF_ID: " << processor.IF_ID << std::endl;
  os << std::string(32, '-') << std::endl;

  os << "decoder: " << processor.decoder << std::endl;
  os << "control: " << processor.control << std::endl;
  os << "registers: " << processor.registers << std::endl;
  os << "immGen: " << processor.immGen << std::endl;
  os << "branchAddrChooser: " << processor.branchAddrChooser << std::endl;
  os << "branchAddrAlu: " << processor.branchAddrAlu << std::endl;
  os << "branchDecisionAluControl: " << processor.branchDecisionAluControl << std::endl;
  os << "branchDecisionAlu: " << processor.branchDecisionAlu << std::endl;
  os << "condBranchDecisionMaker: " << processor.condBranchDecisionMaker << std::endl;
  os << "branchDecisionMaker: " << processor.branchDecisionMaker << std::endl;
  os << "ID_EX: " << processor.ID_EX << std::endl;
  os << std::string(32, '-') << std::endl;

  os << "aluSrc2Chooser: " << processor.aluSrc2Chooser << std::endl;
  os << "alu: " << processor.alu << std::endl;
  os << "aluControl: " << processor.aluControl << std::endl;
  os << "EX_MEM: " << processor.EX_MEM << std::endl;
  os << std::string(32, '-') << std::endl;

  os << "dataMemory: " << processor.dataMemory << std::endl;
  os << "MEM_WB: " << processor.MEM_WB << std::endl;
  os << std::string(32, '-') << std::endl;

  os << "writeBackSrcChooser: " << processor.writeBackSrcChooser << std::endl;

  os << std::string(32, '=') << std::endl;

  return os;
}

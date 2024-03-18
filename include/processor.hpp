#ifndef SIMULATOR_PROCESSOR_HPP
#define SIMULATOR_PROCESSOR_HPP

#include "memory.hpp"

#include <bit>
#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <ostream>

constexpr int registerCount = 32;

enum class RegisterType {
  Integer, FloatingPoint
};

// todo: use the updated Word
template<RegisterType T>
struct RegisterFile {
  using Value = std::conditional_t<T == RegisterType::Integer, uint32_t, float>;

  Word regs[registerCount];

  // zero-initialize the registers (no surprises!)
  RegisterFile() {
    std::fill(regs, regs + registerCount, Word(Value()));
  }

  Value readRegister(size_t regNum) const {
    if (regNum >= 32) {
      throw std::out_of_range("register number must be smaller than 32");
    }
    return Value(regs[regNum]);
  }

  void writeRegister(size_t regNum, Value value) {
    if (regNum >= 32) {
      throw std::out_of_range("register number must be smaller than 32");
    }
    // discard writes to x0 as it is hardcoded to integer 0
    if constexpr (T == RegisterType::Integer) {
      if (regNum == 0) return;
    }
    regs[regNum] = Word(value);
  }
};

struct Unit;

// out-in synchronization: outputSignal >> inputSignal >> ...
// only allow changes to occur through synced output signals
struct InputSignal {
  Word val;
  Unit *unit;

  explicit InputSignal(Unit *unit) : unit{unit} {}
};

// can be synchronized to a number of InputSignals
// receiving a new value: outputSignal << newValue
struct OutputSignal {
  Word val;
  std::vector<InputSignal*> syncedInputs;

  // assumes that each InputSignal is synced only ONCE with a particular OutputSignal
  OutputSignal& operator>>(InputSignal &signal);
  void operator<<(Word newValue);
};

struct Unit {
  virtual void notifyInputChange() = 0;
  // performs its functions according to input signals
  virtual void operate() = 0;
};

// out-of-sync units immediately propagate input changes to its outputs
struct OutOfSyncUnit : public Unit {
  void notifyInputChange() override { operate(); }
};

// in-sync units only propagate input changes to its outputs when clock ticks
//  - used in conjunction with OutOfSync units to simulate functional units
//  - the Processor is responsible for synchronizing InSyncUnits to the clock
struct InSyncUnit : public Unit {
  void notifyInputChange() override { }
};

struct DecodeUnit : public OutOfSyncUnit {
  InputSignal instruction{this};
  OutputSignal readRegister1;
  OutputSignal readRegister2;
  OutputSignal writeRegister;
  OutputSignal func3;
  OutputSignal func7;

  void operate() override;
};

// handles only R-type, I-type, lw, sw and beq for now
struct ControlUnit : public OutOfSyncUnit {
  InputSignal instruction{this};
  OutputSignal ctrlRegWrite;
  OutputSignal ctrlAluSrc;
  OutputSignal ctrlAluOp;
  OutputSignal ctrlBranch;
  OutputSignal ctrlMemWrite;
  OutputSignal ctrlMemRead;
  OutputSignal ctrlMemToReg;

  void operate() override;
};

struct RegisterFileUnit : public OutOfSyncUnit {
  InputSignal readRegister1{this};
  InputSignal readRegister2{this};
  InputSignal writeRegister{this};
  InputSignal writeData{this};
  InputSignal ctrlRegWrite{this};
  OutputSignal readData1;
  OutputSignal readData2;

  // todo: perhaps we should make the RegisterFile fail silently as invalid
  // inputs are not only possible but valid
  RegisterFile<RegisterType::Integer> intRegs;

  void operate() override;
};

// handles only addi, lw and sw (I and S-type instructions)
struct ImmediateGenerator : public OutOfSyncUnit {
  InputSignal instruction{this};
  OutputSignal immediate;

  void operate() override;
};

// generic 2-way multiplexer
struct Multiplexer : public OutOfSyncUnit {
  InputSignal input0{this};
  InputSignal input1{this};
  InputSignal control{this};
  OutputSignal output;

  void operate() override;
};

// only support a limited set of operations now
enum class ALUOp : uint32_t {
  Add, Sub, And, Or
};

// the alu bits that control the alu are a departure from how alu bits actually work in
// hardware (as described in Patterson-Hennessy)
// only handles add, addi, sub, beq, lw and sw
struct ALUControl : public OutOfSyncUnit {
  // actually only need the func3 and func7 fields
  InputSignal instruction{this};
  InputSignal ctrlAluOp{this};
  OutputSignal aluOp;

  void operate() override;
};

struct ALUUnit : public OutOfSyncUnit {
  InputSignal input0{this};
  InputSignal input1{this};
  InputSignal aluOp{this};
  OutputSignal output;
  OutputSignal zero;

  void operate() override;
};

// we will most likely need output signal(s) that asserts when the memory unit has
// finished its write or read operation
// for now, only use main memory with an 8-bit address space
struct DataMemoryUnit : public OutOfSyncUnit {
  InputSignal address{this};
  InputSignal writeData{this};
  InputSignal ctrlMemRead{this};
  InputSignal ctrlMemWrite{this};
  OutputSignal readData;

  MainMemory<8> memory;

  void operate() override;
};

// 8-bit address space: no more than 256 instructions for now
struct InstructionMemoryUnit : public OutOfSyncUnit {
  InputSignal address{this};
  OutputSignal instruction;

  MainMemory<8> memory; 

  void operate() override;
};

struct AndGate : public OutOfSyncUnit {
  InputSignal input0{this};
  InputSignal input1{this};
  OutputSignal output;

  void operate() override;
};

struct IFIDRegisters : public InSyncUnit {
  InputSignal pcIn{this};
  InputSignal instructionIn{this};

  OutputSignal pcOut;
  OutputSignal instructionOut;

  void operate() override;
};

// error-prone list of control signals (todo: is there a better way?)
struct IDEXRegisters : public InSyncUnit {
  InputSignal readData1In{this};
  InputSignal readData2In{this};
  InputSignal immediateIn{this};
  InputSignal instructionIn{this};

  OutputSignal readData1Out;
  OutputSignal readData2Out;
  OutputSignal immediateOut;
  OutputSignal instructionOut;

  InputSignal ctrlAluSrcIn{this};
  InputSignal ctrlAluOpIn{this};
  InputSignal ctrlBranchIn{this};
  InputSignal ctrlMemWriteIn{this};
  InputSignal ctrlMemReadIn{this};
  InputSignal ctrlMemToRegIn{this};
  InputSignal ctrlRegWriteIn{this};
  InputSignal writeRegisterIn{this};
  InputSignal pcIn{this};

  OutputSignal ctrlAluSrcOut;
  OutputSignal ctrlAluOpOut;
  OutputSignal ctrlBranchOut;
  OutputSignal ctrlMemWriteOut;
  OutputSignal ctrlMemReadOut;
  OutputSignal ctrlMemToRegOut;
  OutputSignal ctrlRegWriteOut;
  OutputSignal writeRegisterOut;
  OutputSignal pcOut;

  void operate() override;
};

struct EXMEMRegisters : public InSyncUnit {
  InputSignal branchAdderOutputIn{this};
  InputSignal zeroIn{this};
  InputSignal aluOutputIn{this};
  InputSignal readData2In{this};

  OutputSignal branchAdderOutputOut;
  OutputSignal zeroOut;
  OutputSignal aluOutputOut;
  OutputSignal readData2Out;

  InputSignal ctrlBranchIn{this};
  InputSignal ctrlMemWriteIn{this};
  InputSignal ctrlMemReadIn{this};
  InputSignal ctrlMemToRegIn{this};
  InputSignal ctrlRegWriteIn{this};
  InputSignal writeRegisterIn{this};

  OutputSignal ctrlBranchOut;
  OutputSignal ctrlMemWriteOut;
  OutputSignal ctrlMemReadOut;
  OutputSignal ctrlMemToRegOut;
  OutputSignal ctrlRegWriteOut;
  OutputSignal writeRegisterOut;

  void operate() override;
};

struct MEMWBRegisters : public InSyncUnit {
  InputSignal readMemoryDataIn{this};
  InputSignal aluOutputIn{this};

  OutputSignal readMemoryDataOut;
  OutputSignal aluOutputOut;

  InputSignal ctrlMemToRegIn{this};
  InputSignal ctrlRegWriteIn{this};
  InputSignal writeRegisterIn{this};

  OutputSignal ctrlMemToRegOut;
  OutputSignal ctrlRegWriteOut;
  OutputSignal writeRegisterOut;

  void operate() override;
};

struct InstructionIssueUnit : public InSyncUnit {
  InputSignal pcIn{this};
  OutputSignal pcOut;

  void operate() override;
};

// the processor is responsible for registering the InSyncUnits that it uses into
// the syncedUnits list
struct Processor {
  std::vector<InSyncUnit*> syncedUnits;
  int clockCycle = 0;

  virtual void executeOneCycle() {
    ++clockCycle;
    for (InSyncUnit *unit : syncedUnits) {
      unit->operate();
    }
  }
};

struct PipelinedProcessor : public Processor {
  Multiplexer pcChooser;
  InstructionIssueUnit issueUnit;
  ALUUnit pcAdder;
  InstructionMemoryUnit instructionMemory;

  IFIDRegisters IF_ID;
  DecodeUnit decoder;
  ControlUnit control;
  RegisterFileUnit registers;
  ImmediateGenerator immGen;

  IDEXRegisters ID_EX;
  ALUUnit branchAdder;    // alu's default op is addition; reuse
  Multiplexer aluSrc2Chooser;
  ALUUnit alu;
  ALUControl aluControl;

  EXMEMRegisters EX_MEM;
  AndGate branchChooser;
  DataMemoryUnit dataMemory;

  MEMWBRegisters MEM_WB;
  Multiplexer writeBackSrcChooser;

  PipelinedProcessor();
  void registerInSyncUnits();   // should only be called ONCE by constructor
  void synchronizeSignals();    // should also be called ONCE by constructor
};

// ostream& operator<<(ostream& os, const Date& dt)

// pretty-printing funcs for the signals and functional units
std::ostream& operator<<(std::ostream& os, const InputSignal &input);
std::ostream& operator<<(std::ostream& os, const OutputSignal &output);

std::ostream& operator<<(std::ostream& os, const DecodeUnit &decoder);
std::ostream& operator<<(std::ostream& os, const ControlUnit &control);
std::ostream& operator<<(std::ostream& os, const RegisterFileUnit &registers);
std::ostream& operator<<(std::ostream& os, const ImmediateGenerator &immGen);
std::ostream& operator<<(std::ostream& os, const Multiplexer &mux);
std::ostream& operator<<(std::ostream& os, const ALUControl &aluControl);
std::ostream& operator<<(std::ostream& os, const ALUUnit &alu);
std::ostream& operator<<(std::ostream& os, const DataMemoryUnit &dataMemory);
std::ostream& operator<<(std::ostream& os, const InstructionMemoryUnit &instructionMemory);
std::ostream& operator<<(std::ostream& os, const AndGate &andGate);
std::ostream& operator<<(std::ostream& os, const IFIDRegisters &IF_ID);
std::ostream& operator<<(std::ostream& os, const IDEXRegisters &ID_EX);
std::ostream& operator<<(std::ostream& os, const EXMEMRegisters &EX_MEM);
std::ostream& operator<<(std::ostream& os, const MEMWBRegisters &MEM_WB);
std::ostream& operator<<(std::ostream& os, const InstructionIssueUnit &issueUnit);

std::ostream& operator<<(std::ostream& os, const PipelinedProcessor &processor);

#endif

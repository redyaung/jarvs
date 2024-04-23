#ifndef SIMULATOR_PROCESSOR_HPP
#define SIMULATOR_PROCESSOR_HPP

#include "memory.hpp"

#include <bit>
#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <ostream>
#include <concepts>
#include <optional>

constexpr int registerCount = 32;

enum class RegisterType {
  Integer, FloatingPoint
};

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

struct Unit {
  virtual void notifyInputChange() = 0;
  // performs its functions according to input signals
  virtual void operate() = 0;
};

template<size_t W>
struct WideInputSignal {
  Block<W> val;
  Unit *unit;

  explicit WideInputSignal(Unit *unit) : unit{unit} {}
  void changeValue(const Block<W> &newVal) {
    val = newVal;
    unit->notifyInputChange();
  }
  const Block<W> &operator*() const { return val; }
  Block<W> &operator*() { return val; }
};

template<size_t W>
struct WideOutputSignal {
  Block<W> val;
  std::vector<WideInputSignal<W>*> syncedInputs;

  WideOutputSignal<W>& operator>>(WideInputSignal<W> &signal) {
    syncedInputs.push_back(&signal);
    return *this;
  }
  void operator<<(const Block<W> &newVal) {
    val = newVal;
    for (WideInputSignal<W> *inSignal : syncedInputs) {
      inSignal->changeValue(newVal);
    }
  }
  const Block<W> &operator*() const { return val; }
  Block<W> &operator*() { return val; }
};

// out-in synchronization: outputSignal >> inputSignal >> ...
// only allow changes to occur through synced output signals
struct InputSignal : WideInputSignal<1> {
  using WideInputSignal<1>::WideInputSignal;

  Word operator*() const { return val[0]; }
  Word& operator*() { return val[0]; }
};

// can be synchronized to a number of InputSignals
// receiving a new value: outputSignal << newValue
struct OutputSignal : WideOutputSignal<1> {
  using WideOutputSignal<1>::WideOutputSignal;

  void operator<<(Word newVal) {
    WideOutputSignal<1>::operator<<(Block<1>({newVal}));
  }
  Word operator*() const { return val[0]; }
  Word& operator*() { return val[0]; }
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

// buffered in-sync units perform a two-stage propagation:
//  1. input changes are first propagated once to an internal buffer
//  2. when the clock ticks, the output is updated according to the buffers
// buffering allows us to execute in-sync units out-of-order irrespectively of data flow
// since the inputs can be buffered even before the clock ticks
//
// rules:
//  1. the Processor is responsible for calling bufferInputs() on all BufferedInSyncUnits
//     before calling operate()
//  2. when synchronizing the inputs and output signals of buffered units, use only
//      i.  .out >> [foo] (.buffer >> [...] is internal)
//      ii. [bar] >> .buffer ([...] >> out is internal)
struct BufferedInSyncUnit : public InSyncUnit {
  virtual void bufferInputs() = 0;
};

// a frozen pipeline register will not change its contents
struct Freezable : virtual public InSyncUnit {
  InputSignal shouldFreeze{this};
};

// a set of pipeline registers is flushed if it is set to reflect a nop
struct Flushable : virtual public InSyncUnit {
  InputSignal shouldFlush{this};

  Word zeroOnFlush(Word val) const;
};

struct AndGate : public OutOfSyncUnit {
  InputSignal input0{this};
  InputSignal input1{this};
  OutputSignal output;

  void operate() override;
};

struct OrGate : public OutOfSyncUnit {
  InputSignal input0{this};
  InputSignal input1{this};
  OutputSignal output;

  void operate() override;
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

struct RegisterFileUnit : public OutOfSyncUnit {
  InputSignal readRegister1{this};
  InputSignal readRegister2{this};
  InputSignal writeRegister{this};
  InputSignal writeData{this};
  InputSignal ctrlRegWrite{this};
  OutputSignal readData1;
  OutputSignal readData2;

  // suggestion: perhaps we should make the RegisterFile fail silently as invalid
  // inputs are not only possible but valid
  RegisterFile<RegisterType::Integer> intRegs;

  void operate() override;
};

// handles only R-type, I-type, lw, sw and beq for now
struct ControlUnit : public OutOfSyncUnit {
  InputSignal instruction{this};
  InputSignal pc{this};
  InputSignal writeRegister{this};

  OutputSignal ctrlRegWrite;
  OutputSignal ctrlAluSrc;
  OutputSignal ctrlAluOp;
  OutputSignal ctrlMemWrite;
  OutputSignal ctrlMemRead;
  OutputSignal ctrlMemToReg;
  OutputSignal ctrlBranch;
  OutputSignal ctrlUseRegBase;
  OutputSignal ctrlIsJump;

  RegisterFile<RegisterType::Integer> *intRegs;

  ControlUnit(RegisterFile<RegisterType::Integer> *intRegs);
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
  Add, Sub, Or, And, Sll, Srl
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

// used only by branch alu and control
enum class BranchALUOp : uint32_t {
  Eq, Ne, Lt, Ge
};

// controls a specialized branch alu unit
//  - currently supports all bxx instructions except bltu and bgeu
struct BranchALUControl : public OutOfSyncUnit {
  InputSignal func3{this};
  OutputSignal branchAluOp;

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

struct BranchALUUnit : public OutOfSyncUnit {
  InputSignal input0{this};
  InputSignal input1{this};
  InputSignal branchAluOp{this};
  OutputSignal output;    // 1 = should branch

  void operate() override;
};

// W: number of words in each block
template<size_t AddressSpace, uint32_t W>
struct __MainMemoryUnit : public InSyncUnit {
  InputSignal isRead{this};
  InputSignal shouldOperate{this};
  InputSignal address{this};
  WideInputSignal<W> writeData{this};

  WideOutputSignal<W> readData;
  OutputSignal isReady;

  const size_t latency;     // how many cycles it takes to complete an access

  bool isCurrentlyReading = false;
  int cyclesOperated = 0;

  std::byte bytes[1<< AddressSpace];

  __MainMemoryUnit(size_t latency = 1) : latency{latency} {
    std::fill(bytes, bytes + (1<< AddressSpace), std::byte{0});
  }

  inline bool isOperating() const {
    return cyclesOperated > 0;
  }

  // invariant: whenever the memory unit is operating, the current request should be
  // consistent with the request made previously, i.e., if we requested a read()
  // in the last cycle, the current request must also be a read()
  bool checkInvariant() const {
    if (!isOperating()) {
      return true;
    }
    return *shouldOperate && isCurrentlyReading == *isRead;
  }

  // assumes invariant has been met
  bool willCompleteOperation() const {
    return cyclesOperated + 1 >= latency;
  }

  void operate() override {
    assert(checkInvariant());
    if (!*shouldOperate) {
      isReady << true;
      return;
    }
    isCurrentlyReading = *isRead;
    if (!willCompleteOperation()) {
      isReady << false;
      cyclesOperated++;
      return;
    }
    if (*isRead) {   // read
      readData << readBlock<W>(*address);
    } else {            // write
      writeBlock(*address, *writeData);
    }
    isReady << true;
    cyclesOperated = 0;
  }

  template<uint32_t _W>
  Block<_W> readBlock(uint32_t addr) {
    if (!isAligned(addr, _W)) {
      throw std::invalid_argument("address " + std::to_string(addr) + " is not word-aligned");
    } else if (addr + nbytes(_W) > (1<< AddressSpace)) {
      throw std::out_of_range("end address is out of address space");
    }
    Block<_W> block;
    std::copy(bytes + addr, bytes + addr + nbytes(_W), block.bytes);
    return block;
  }

  template<uint32_t _W>
  void writeBlock(uint32_t addr, Block<_W> block) {
    if (!isAligned(addr, _W)) {
      throw std::invalid_argument("address " + std::to_string(addr) + " is not word-aligned");
    } else if (addr + nbytes(_W) > (1<< AddressSpace)) {
      throw std::out_of_range("end address is out of address space");
    }
    std::copy(block.bytes, block.bytes + nbytes(_W), bytes + addr);
  }
};

// widens an InputSignal to a WideOutputSignal<1>
struct SignalWideningUnit : public OutOfSyncUnit {
  InputSignal in{this};
  WideOutputSignal<1> out;

  void operate() override;
};

// shortens a WideInputSignal<1> to an OutputSignal
struct SignalShorteningUnit : public OutOfSyncUnit {
  WideInputSignal<1> in{this};
  OutputSignal out;

  void operate() override;
};

struct __DataMemoryUnit : public InSyncUnit {
  // note: the template argument W must be 1 here as the memory unit accesses words
  __MainMemoryUnit<8, 1> memory;

  InputSignal &ctrlMemRead = memory.isRead;
  InputSignal &address = memory.address;
  InputSignal &writeData = __writeDataWidener.in;
  InputSignal ctrlMemWrite{this};

  OutputSignal &readData = __readDataShortener.out;
  OutputSignal &isReady = memory.isReady;

  OutputSignal __isRead;
  OutputSignal __isWrite;
  OrGate __shouldOperate;
  SignalWideningUnit __writeDataWidener;
  SignalShorteningUnit __readDataShortener;

  __DataMemoryUnit(size_t latency);
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

struct IFIDRegisters : public Freezable, public Flushable {
  InputSignal pcIn{this};
  InputSignal instructionIn{this};

  OutputSignal pcOut;
  OutputSignal instructionOut;

  void operate() override;
};

// error-prone list of control signals (suggestion: is there a better way?)
struct IDEXRegisters : public Freezable, public Flushable {
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
  InputSignal ctrlMemWriteIn{this};
  InputSignal ctrlMemReadIn{this};
  InputSignal ctrlMemToRegIn{this};
  InputSignal ctrlRegWriteIn{this};
  InputSignal writeRegisterIn{this};
  InputSignal readRegister1In{this};
  InputSignal readRegister2In{this};

  OutputSignal ctrlAluSrcOut;
  OutputSignal ctrlAluOpOut;
  OutputSignal ctrlMemWriteOut;
  OutputSignal ctrlMemReadOut;
  OutputSignal ctrlMemToRegOut;
  OutputSignal ctrlRegWriteOut;
  OutputSignal writeRegisterOut;
  OutputSignal readRegister1Out;
  OutputSignal readRegister2Out;

  // only used for analytics and visualization
  InputSignal pcIn{this};
  OutputSignal pcOut;

  void operate() override;
};

struct EXMEMRegisters : public Freezable, public Flushable {
  InputSignal zeroIn{this};
  InputSignal aluOutputIn{this};
  InputSignal readData2In{this};

  OutputSignal zeroOut;
  OutputSignal aluOutputOut;
  OutputSignal readData2Out;

  InputSignal ctrlMemWriteIn{this};
  InputSignal ctrlMemReadIn{this};
  InputSignal ctrlMemToRegIn{this};
  InputSignal ctrlRegWriteIn{this};
  InputSignal writeRegisterIn{this};

  OutputSignal ctrlMemWriteOut;
  OutputSignal ctrlMemReadOut;
  OutputSignal ctrlMemToRegOut;
  OutputSignal ctrlRegWriteOut;
  OutputSignal writeRegisterOut;

  // only used for analytics and visualization
  InputSignal pcIn{this};
  InputSignal instructionIn{this};
  OutputSignal pcOut;
  OutputSignal instructionOut;

  void operate() override;
};

// this doesn't need to be flushable nor freezable, because the latest source of flushes
// and stalls in the pipeline is in the MEM stage which is *before* the MEM/WB pipeline
// registers
struct MEMWBRegisters : public Flushable {
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

  // only used for analytics and visualization
  InputSignal pcIn{this};
  InputSignal instructionIn{this};
  OutputSignal pcOut;
  OutputSignal instructionOut;

  void operate() override;
};

struct InstructionIssueUnit : public Freezable {
  InputSignal pcIn{this};
  OutputSignal pcOut;

  void operate() override;
};

// subclass ConcreteBufferedUnit to define *concrete* (lol) buffer units
//  - the parent class cannot synchronize the signals between the buffer and output
//    units, so this must be done in the c-tor of the subclass
template<std::derived_from<InSyncUnit> U>
struct ConcreteBufferedUnit : public BufferedInSyncUnit {
  U buffer, out;

  // propagate shouldFreeze and shouldFlush explicitly
  void bufferInputs() override { 
    if constexpr (std::derived_from<U, Freezable>) {
      *out.shouldFreeze = *buffer.shouldFreeze;
    }
    if constexpr (std::derived_from<U, Flushable>) {
      *out.shouldFlush = *buffer.shouldFlush;
    }
    buffer.operate();
  }

  void operate() override { out.operate(); }
};

struct BufferedMEMWBRegisters : public ConcreteBufferedUnit<MEMWBRegisters> {
  BufferedMEMWBRegisters();
};

struct BufferedInstructionIssueUnit : public ConcreteBufferedUnit<InstructionIssueUnit> {
  BufferedInstructionIssueUnit();

};

struct ForwardingUnit : public InSyncUnit {
  // quite ugly; would be great if this can be manipulated like MEMWBRegisters
  IDEXRegisters *ID_EX;
  EXMEMRegisters *EX_MEM;
  BufferedMEMWBRegisters *MEM_WB;

  ForwardingUnit(
    IDEXRegisters &ID_EX,
    EXMEMRegisters &EX_MEM,
    BufferedMEMWBRegisters &MEM_WB
  );
  void operate() override;
};

// todo (urgent): need a separate OutOfSync MemoryHazardDetection unit
//  - currently, tests are failing after propagating shouldFreeze and shouldFlush
//  from buffer to out (which should not)
//  - this division is needed because DataHazardDetectionUnit runs before any
//  other unit and cannot take action immediately when MemoryUnit has finished
//  running. it would only take action in the next cycle which is not what we
//  want

struct MemoryHazardDetectionUnit : public InSyncUnit {
  InputSignal isDataMemoryReady{this};

  OutputSignal shouldFreezeIssue;   // in common w/ DataHazardDetectionUnit
  OutputSignal shouldFreezeIF_ID;
  OutputSignal shouldFreezeID_EX;
  OutputSignal shouldFreezeEX_MEM;
  OutputSignal shouldFlushMEM_WB;

  void operate() override;
};

// must be called before the forwarding unit
struct DataHazardDetectionUnit : public InSyncUnit {
  bool isForwarding;

  // need these pointers to determine whether there is a data hazard
  BufferedInstructionIssueUnit *issueUnit;
  IFIDRegisters *IF_ID;
  IDEXRegisters *ID_EX;
  EXMEMRegisters *EX_MEM;

  OutputSignal shouldFreezeIssue;   // in common w/ MemoryHazardDetectionUnit
  OutputSignal shouldFlushIF_ID;

  DataHazardDetectionUnit(
    bool isForwarding,
    BufferedInstructionIssueUnit &issueUnit,
    IFIDRegisters &IF_ID,
    IDEXRegisters &ID_EX,
    EXMEMRegisters &EX_MEM
  );
  void operate() override;
  bool hasDataHazard(uint32_t rs1, uint32_t rs2);
};

// the processor is responsible for registering the appropriate synchronized units,
// buffered units and priority units.
//  - priority units usually contain the forwarding and hazard detection unit
//  - buffered units are used to break execution order dependencies between sync units
struct Processor {
  std::vector<InSyncUnit*> syncedUnits;
  std::vector<BufferedInSyncUnit*> bufferedUnits;
  std::vector<InSyncUnit*> priorityUnits;
  int clockCycle = 0;

  virtual void executeOneCycle() {
    ++clockCycle;
    for (InSyncUnit *unit : priorityUnits) {
      unit->operate();
    }
    for (BufferedInSyncUnit *unit : bufferedUnits) {
      unit->bufferInputs();
    }
    for (InSyncUnit *unit : syncedUnits) {
      unit->operate();
    }
  }
};

struct PipelinedProcessor : public Processor {
  // fetch (IF)
  Multiplexer pcChooser;
  OrGate issueUnitFreezeDecisionMaker;
  BufferedInstructionIssueUnit issueUnit;
  ALUUnit pcAdder;    // todo: make this a dedicated adder
  InstructionMemoryUnit instructionMemory;
  OrGate IF_ID_flushDecisionMaker;
  IFIDRegisters IF_ID;

  // decode (ID)
  DecodeUnit decoder;
  RegisterFileUnit registers;
  ControlUnit control;
  ImmediateGenerator immGen;
  Multiplexer branchAddrChooser;
  ALUUnit branchAddrAlu;    // todo: make this a dedicated adder (same as pcAdder)
  BranchALUControl branchDecisionAluControl;
  BranchALUUnit branchDecisionAlu;
  AndGate condBranchDecisionMaker;
  OrGate branchDecisionMaker;
  IDEXRegisters ID_EX;

  // execute (EX)
  Multiplexer aluSrc2Chooser;
  ALUUnit alu;
  ALUControl aluControl;
  EXMEMRegisters EX_MEM;

  // memory (MEM)
  __DataMemoryUnit dataMemory;
  BufferedMEMWBRegisters MEM_WB;

  // write-back (WB)
  Multiplexer writeBackSrcChooser;

  // miscellaneous units
  ForwardingUnit forwardingUnit;
  DataHazardDetectionUnit hazardDetectionUnit;
  MemoryHazardDetectionUnit memHazardUnit;

  PipelinedProcessor(bool useForwarding = false, size_t memoryLatency = 1);
  // registers both in-sync units and buffered in-sync units
  void registerUnits(bool useForwarding);   // should only be called ONCE by constructor
  void synchronizeSignals();    // should also be called ONCE by constructor
};

// pretty-printing funcs for the signals and functional units
std::ostream& operator<<(std::ostream& os, const InputSignal &input);
std::ostream& operator<<(std::ostream& os, const OutputSignal &output);

std::ostream& operator<<(std::ostream& os, const DecodeUnit &decoder);
std::ostream& operator<<(std::ostream& os, const ControlUnit &control);
std::ostream& operator<<(std::ostream& os, const RegisterFileUnit &registers);
std::ostream& operator<<(std::ostream& os, const ImmediateGenerator &immGen);
std::ostream& operator<<(std::ostream& os, const Multiplexer &mux);
std::ostream& operator<<(std::ostream& os, const ALUControl &aluControl);
std::ostream& operator<<(std::ostream& os, const BranchALUControl &branchAluControl);
std::ostream& operator<<(std::ostream& os, const ALUUnit &alu);
std::ostream& operator<<(std::ostream& os, const BranchALUUnit &branchAlu);
std::ostream& operator<<(std::ostream& os, const DataMemoryUnit &dataMemory);
std::ostream& operator<<(std::ostream& os, const InstructionMemoryUnit &instructionMemory);
std::ostream& operator<<(std::ostream& os, const AndGate &andGate);
std::ostream& operator<<(std::ostream& os, const OrGate &orGate);
std::ostream& operator<<(std::ostream& os, const IFIDRegisters &IF_ID);
std::ostream& operator<<(std::ostream& os, const IDEXRegisters &ID_EX);
std::ostream& operator<<(std::ostream& os, const EXMEMRegisters &EX_MEM);
std::ostream& operator<<(std::ostream& os, const MEMWBRegisters &MEM_WB);
std::ostream& operator<<(std::ostream& os, const InstructionIssueUnit &issueUnit);
std::ostream& operator<<(std::ostream& os, const BufferedMEMWBRegisters &MEM_WB);
std::ostream& operator<<(std::ostream& os, const BufferedInstructionIssueUnit &issueUnit);

std::ostream& operator<<(std::ostream& os, const PipelinedProcessor &processor);

#endif

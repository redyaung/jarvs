#ifndef SIMULATOR_PROCESSOR_HPP
#define SIMULATOR_PROCESSOR_HPP

#include "memory.hpp"

#include <bit>
#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <vector>

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
struct InSyncUnit : public Unit {
  void notifyInputChange() override { }
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
// only handles add, sub, beq, lw and sw
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
struct MemoryUnit : public OutOfSyncUnit {
  InputSignal address{this};
  InputSignal writeData{this};
  InputSignal ctrlMemRead{this};
  InputSignal ctrlMemWrite{this};
  OutputSignal readData;

  MainMemory<8> memory;

  void operate() override;
};

#endif

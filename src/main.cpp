#include <iostream>
#include "processor.hpp"

int main() {
  PipelinedProcessor processor;

  uint32_t addInstruction = 0b0000000'00011'00010'000'00001'0110011;   // add x1, x2, x3
  processor.instructionMemory.memory.writeBlock(0x0, Block<1>{addInstruction});
  processor.registers.intRegs.writeRegister(2, 6);
  processor.registers.intRegs.writeRegister(3, 7);

  for (int cycle = 0; cycle < 5; cycle++) {
    processor.executeOneCycle();
    std::cout << processor << std::endl << std::endl;
  }

  return 0;
}

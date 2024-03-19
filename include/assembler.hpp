#ifndef SIMULATOR_ASSEMBLER_HPP
#define SIMULATOR_ASSEMBLER_HPP

#include "memory.hpp"

// encodes a human-readable RISC-V instruction into a machine-readable 32-bit format
// currently only supports add, addi, sub, lw, sw
// other limitations:
//  - no leading whitespace
//  - no extraneous whitespaces between each token
Word encodeInstruction(const std::string &line);

#endif

#ifndef SIMULATOR_ASSEMBLER_HPP
#define SIMULATOR_ASSEMBLER_HPP

#include "memory.hpp"
#include <istream>

// encodes a human-readable RISC-V instruction into a machine-readable 32-bit format
// currently only supports add, addi, sub, lw, sw
// other limitations:
//  - no extraneous whitespaces between each token
//  - branch instructions must not use labels and instead use explicit offsets
//  - the input string must contain a valid instruction (eg. cannot be an empty line)
Word encodeInstruction(const std::string &line);

// can skip empty lines (has the same restrictions as encodeInstruction)
std::vector<Word> encodeInstructions(std::istream &is);

#endif

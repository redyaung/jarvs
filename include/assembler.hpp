#ifndef ASSEMBLER_HPP
#define ASSEMBLER_HPP

#include "memory.hpp"

// encodes a human-readable RISC-V instruction into a machine-readable 32-bit format
// currently only parses add, addi, sub, lw, sw
Word encodeInstruction(const std::string &line);

#endif

#include "assembler.hpp"
#include <sstream>
#include <stdexcept>

enum class InstructionType {
  Add, Sub, Addi, Lw, Sw
};

struct Instruction {
  InstructionType type;
  uint8_t rd;
  uint8_t rs1;
  uint8_t rs2;
  int immediate;
};

uint8_t parseRegister(std::stringstream &stream) {
  stream.get();   // skip the space
  if (stream.get() != 'x') {    // register name must start with x
    throw std::invalid_argument("register name must start with x");
  }
  int registerNum;
  stream >> registerNum;
  if (registerNum < 0 || registerNum >= 32) {
    throw std::invalid_argument("register number must be between 0 and 31");
  }
  return static_cast<uint8_t>(registerNum);
}

int parseImmediate(std::stringstream &stream) {
  int immediate;
  stream >> immediate;
  return immediate;
}

// limitations:
//  - no leading or trailing whitespace
//  - instruction must be in lowercase
//  - no extra whitespace between tokens (there must still be a space after comma)
//  - immediates can only be decimal numbers
//  - lw, sw must be specified in non bracket form
Instruction parseInstruction(const std::string &line) {
  std::stringstream stream(line);
  std::string instruction;
  stream >> instruction;
  if (instruction == "add" || instruction == "sub") {
    InstructionType type = instruction == "add" ? InstructionType::Add : InstructionType::Sub;
    uint8_t rd = parseRegister(stream); stream.get();   // consume the comma
    uint8_t rs1 = parseRegister(stream); stream.get();
    uint8_t rs2 = parseRegister(stream);
    return Instruction{type, rd, rs1, rs2};
  } else if (instruction == "addi" || instruction == "lw") {
    InstructionType type = instruction == "addi" ? InstructionType::Addi : InstructionType::Lw;
    uint8_t rd = parseRegister(stream); stream.get();
    uint8_t rs1 = parseRegister(stream); stream.get();
    int imm = parseImmediate(stream);
    return Instruction{type, rd, rs1, 0, imm};
  } else if (instruction == "sw") {
    uint8_t rs2 = parseRegister(stream); stream.get();
    uint8_t rs1 = parseRegister(stream); stream.get();
    int imm = parseImmediate(stream);
    return Instruction{InstructionType::Sw, 0, rs1, rs2, imm};
  } else {
    throw std::invalid_argument(instruction + " instruction type cannot be parsed");
  }
}

constexpr uint32_t placeBits(uint32_t source, int start, int end, uint32_t bits) {
  uint32_t mask = (1u << (end - start + 1)) - 1;
  uint32_t emptiedSource = source & ~(mask<< start);
  return emptiedSource | ((bits & mask)<< start);
}

// a copy from processor
constexpr uint32_t extractBits(uint32_t val, int start, int end) {
  uint32_t mask = ((1u << (end - start + 1)) - 1)<< start;
  return (val & mask) >> start;
}

Word encodeInstruction(Instruction instruction) {
  if (instruction.type == InstructionType::Add || instruction.type == InstructionType::Sub) {
    uint32_t opcode = 0b0110011;
    uint32_t func3 = 0x0;
    uint32_t func7 = instruction.type == InstructionType::Add ? 0x00 : 0x20;
    uint32_t encoded = 0x0;
    encoded = placeBits(encoded, 0, 6, opcode);
    encoded = placeBits(encoded, 7, 11, instruction.rd);
    encoded = placeBits(encoded, 12, 14, func3);
    encoded = placeBits(encoded, 15, 19, instruction.rs1);
    encoded = placeBits(encoded, 20, 24, instruction.rs2);
    encoded = placeBits(encoded, 25, 31, func7);
    return encoded;
  } else if (instruction.type == InstructionType::Addi || instruction.type == InstructionType::Lw) {
    uint32_t opcode = instruction.type == InstructionType::Addi ? 0b0010011 : 0b0000011;
    uint32_t func3 = instruction.type == InstructionType::Addi ? 0x0 : 0x2;
    uint32_t encoded = 0x0;
    encoded = placeBits(encoded, 0, 6, opcode);
    encoded = placeBits(encoded, 7, 11, instruction.rd);
    encoded = placeBits(encoded, 12, 14, func3);
    encoded = placeBits(encoded, 15, 19, instruction.rs1);
    encoded = placeBits(encoded, 20, 31, instruction.immediate);
    return encoded;
  } else if (instruction.type == InstructionType::Sw) {
    uint32_t opcode = 0b0100011;
    uint32_t func3 = 0x2;
    uint32_t lowerImm = extractBits(instruction.immediate, 0, 4);
    uint32_t upperImm = extractBits(instruction.immediate, 5, 11);
    uint32_t encoded = 0x0;
    encoded = placeBits(encoded, 0, 6, opcode);
    encoded = placeBits(encoded, 7, 11, lowerImm);
    encoded = placeBits(encoded, 12, 14, func3);
    encoded = placeBits(encoded, 15, 19, instruction.rs1);
    encoded = placeBits(encoded, 20, 24, instruction.rs2);
    encoded = placeBits(encoded, 25, 31, upperImm);
    return encoded;
  } else {
    throw std::invalid_argument("instruction cannot be encoded");
  }
}

Word encodeInstruction(const std::string &line) {
  return encodeInstruction(parseInstruction(line));
}

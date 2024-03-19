#include "assembler.hpp"
#include "utils.hpp"
#include <variant>
#include <regex>
#include <stdexcept>
#include <unordered_map>
#include <string>

namespace _Assembler {
  // treat S = SB and U = UJ for now
  struct R {
    uint32_t opcode : 7;
    uint32_t rd : 5;
    uint32_t func3 : 3;
    uint32_t rs1 : 5;
    uint32_t rs2 : 5;
    uint32_t func7 : 7;
  };
  struct I {
    uint32_t opcode : 7;
    uint32_t rd : 5;
    uint32_t func3 : 3;
    uint32_t rs1 : 5;
    uint32_t imm : 12;
  };
  struct S {
    uint32_t opcode : 7;
    uint32_t lowerImm : 5;
    uint32_t func3 : 3;
    uint32_t rs1 : 5;
    uint32_t rs2 : 5;
    uint32_t upperImm : 7;
  };
  struct U {
    uint32_t opcode : 7;
    uint32_t rd : 5;
    uint32_t imm : 20;
  };
  using ParsedInstruction = std::variant<R, I, S, U>;

  std::regex RFormatRegex(
    "^([a-z]+) x(\\d{1,2}), x(\\d{1,2}), x(\\d{1,2})"
  );
  std::regex IAndSFormatRegex(
    "^([a-z]+) x(\\d{1,2}), x(\\d{1,2}), (\\d+)"
  );
  std::regex LoadStoreRegex(
    "^([a-z]+) x(\\d{1,2}), (\\d+)\\(x\\d{2}\\)"
  );
  std::regex UFormatRegex(
    "^([a-z]+) x(\\d{1,2}), (\\d+)"
  );

  struct PossibleBitFields {
    uint32_t opcode;
    uint32_t func3;
    uint32_t func7;
  };
  std::unordered_map<std::string, PossibleBitFields> RFieldsLookup {
    {"add",   {0b0110011, 0x0, 0x00}},
    {"sub",   {0b0110011, 0x0, 0x20}}
  };
  std::unordered_map<std::string, PossibleBitFields> IFieldsLookup {
    {"addi",  {0b0010011, 0x0, 0xDEAD}},
    {"lw",    {0b0000011, 0x2, 0xDEAD}}
  };
  std::unordered_map<std::string, PossibleBitFields> SFieldsLookup {
    {"sw",    {0b0100011, 0x2, 0xDEAD}}
  };
  std::unordered_map<std::string, PossibleBitFields> UFieldsLookup {

  };

  void checkRegisterNumber(uint32_t regNum, const std::string &regType) {
    if (regNum < 0 || regNum >= 32) {
      throw std::invalid_argument(
        "invalid " + regType + " register num: " + std::to_string(regNum)
      );
    }
  }

  R makeRFormatInstruction(
    const std::string &name, uint32_t rd, uint32_t rs1, uint32_t rs2
  ) {
    checkRegisterNumber(rd, "rd");
    checkRegisterNumber(rs1, "rs1");
    checkRegisterNumber(rs2, "rs2");
    if (auto result = RFieldsLookup.find(name); result != RFieldsLookup.end()) {
      PossibleBitFields fields = result->second;
      return R{
        fields.opcode,
        rd,
        fields.func3,
        rs1,
        rs2,
        fields.func7
      };
    } else {
      throw std::invalid_argument("unsupported R-type instruction: " + name);
    }
  }

  I makeIFormatInstruction(
    const std::string &name, uint32_t rd, uint32_t rs1, uint32_t imm
  ) {
    checkRegisterNumber(rd, "rd");
    checkRegisterNumber(rs1, "rs1");
    if (auto result = IFieldsLookup.find(name); result != IFieldsLookup.end()) {
      PossibleBitFields fields = result->second;
      return I{
        fields.opcode,
        rd,
        fields.func3,
        rs1,
        imm
      };
    } else {
      throw std::invalid_argument("unsupported I-type instruction: " + name);
    }
  }

  S makeSFormatInstruction(
    const std::string &name, uint32_t rs1, uint32_t rs2, uint32_t lowerImm, uint32_t upperImm
  ) {
    checkRegisterNumber(rs1, "rs1");
    checkRegisterNumber(rs2, "rs2");
    if (auto result = SFieldsLookup.find(name); result != SFieldsLookup.end()) {
      PossibleBitFields fields = result->second;
      return S{
        fields.opcode,
        lowerImm,
        fields.func3,
        rs1,
        rs2,
        upperImm
      };
    } else {
      throw std::invalid_argument("unsupported S-type instruction: " + name);
    }
  }

  U makeUFormatInstruction(
    const std::string &name, uint32_t rd, uint32_t imm
  ) {
    checkRegisterNumber(rd, "rd");
    if (auto result = UFieldsLookup.find(name); result != UFieldsLookup.end()) {
      PossibleBitFields fields = result->second;
      return U{
        fields.opcode,
        rd,
        imm
      };
    } else {
      throw std::invalid_argument("unsupported U-type instruction: " + name);
    }
  }

  ParsedInstruction parseInstruction(const std::string &line) {
    std::smatch matches;
    if (std::regex_match(line, matches, RFormatRegex)) {
      std::string name{matches[1]};
      uint32_t rd = std::stoul(matches[2].str());
      uint32_t rs1 = std::stoul(matches[3].str());
      uint32_t rs2 = std::stoul(matches[4].str());
      return makeRFormatInstruction(name, rd, rs1, rs2);
    } else if (std::regex_match(line, matches, IAndSFormatRegex)) {
      std::string name{matches[1]};
      uint32_t r0 = std::stoul(matches[2].str());
      uint32_t r1 = std::stoul(matches[3].str());
      uint32_t imm = std::stoul(matches[4].str());
      if (IFieldsLookup.contains(name)) {   // I-format
        return makeIFormatInstruction(name, r0, r1, imm);
      } else {    // S-format
        uint32_t lowerImm = extractBits(imm, 0, 4);
        uint32_t upperImm = extractBits(imm, 5, 11);
        return makeSFormatInstruction(name, r1, r0, lowerImm, upperImm);
      }
    } else if (std::regex_match(line, matches, LoadStoreRegex)) {
      std::string name{matches[1]};
      uint32_t r0 = std::stoul(matches[2].str());
      uint32_t imm = std::stoul(matches[3].str());
      uint32_t r1 = std::stoul(matches[4].str());
      if (IFieldsLookup.contains(name)) {   // I-format
        return makeIFormatInstruction(name, r0, r1, imm);
      } else {    // S-format
        uint32_t lowerImm = extractBits(imm, 0, 4);
        uint32_t upperImm = extractBits(imm, 5, 11);
        return makeSFormatInstruction(name, r1, r0, lowerImm, upperImm);
      }
    } else if (std::regex_match(line, matches, UFormatRegex)) {
      std::string name{matches[1]};
      uint32_t rd = std::stoul(matches[2].str());
      uint32_t imm = std::stoul(matches[3].str());
      return makeUFormatInstruction(name, rd, imm);
    } else {
      throw std::invalid_argument("unable to parse: " + line);
    }
  }

  Word encodeInstruction(ParsedInstruction parsed) {
    if (std::holds_alternative<R>(parsed)) {
      return Word::from(std::get<R>(parsed));
    }
    if (std::holds_alternative<I>(parsed)) {
      return Word::from(std::get<I>(parsed));
    }
    if (std::holds_alternative<S>(parsed)) {
      return Word::from(std::get<S>(parsed));
    }
    return Word::from(std::get<U>(parsed));
  }
}

Word encodeInstruction(const std::string &line) {
  return _Assembler::encodeInstruction(_Assembler::parseInstruction(line));
}

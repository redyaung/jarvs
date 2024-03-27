#include "assembler.hpp"
#include "utils.hpp"
#include <variant>
#include <regex>
#include <stdexcept>
#include <unordered_map>
#include <string>
#include <tuple>
#include <optional>

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

  std::regex RFmtRegex(
    "^\\s*([a-z]+) x(\\d{1,2}), x(\\d{1,2}), x(\\d{1,2})"
  );
  std::regex IFmtRegex(
    "^\\s*([a-z]+) x(\\d{1,2}), x(\\d{1,2}), (\\d+)"
  );
  std::regex UFmtRegex(
    "^\\s*([a-z]+) x(\\d{1,2}), (\\d+)"
  );
  std::regex I_FmtRegex(
    "^\\s*([a-z]+) x(\\d{1,2}), (\\d+)\\(x(\\d{1,2})\\)"
  );

  enum class ParseFmt : char {
    R = 0, I, S, SB, U, UJ, I_, S_      // _ : bracketed format (lw)
  };
  constexpr size_t parseFmtCount = 8;   // note: must explicitly set this

  std::unordered_map<ParseFmt, std::regex> regexLookup {
    { ParseFmt::R,  RFmtRegex },
    { ParseFmt::I,  IFmtRegex },
    { ParseFmt::S,  IFmtRegex },
    { ParseFmt::SB, IFmtRegex },
    { ParseFmt::U,  UFmtRegex },
    { ParseFmt::UJ, UFmtRegex },
    { ParseFmt::I_, I_FmtRegex },
    { ParseFmt::S_, I_FmtRegex },
  };

  struct FixedBitFields {
    uint32_t opcode;
    std::optional<uint32_t> func3;
    std::optional<uint32_t> func7;
  };

  // note: new instructions *must* be registered here
  std::unordered_map<std::string, FixedBitFields> fixedFieldsLookup {
    // R
    {"add",   {0b0110011, 0x0, 0x00}},
    {"sub",   {0b0110011, 0x0, 0x20}},

    // I
    {"addi",  {0b0010011, 0x0, std::nullopt}},
    {"lw",    {0b0000011, 0x2, std::nullopt}},
    {"jalr",  {0b1100111, 0x0, std::nullopt}},

    // S
    {"sw",    {0b0100011, 0x2, std::nullopt}},

    // SB
    {"beq",   {0b1100011, 0x0, std::nullopt}},

    // U

    // UJ
    {"jal",   {0b1101111, std::nullopt, std::nullopt}}
  };

  // note: new instructions *must* be registered here
  std::vector<std::string> Rs {
    "add", "sub"
  };
  std::vector<std::string> Is {
    "addi", "lw", "jalr"
  };
  std::vector<std::string> Ss {
    "sw"
  };
  std::vector<std::string> SBs {
    "beq"
  };
  std::vector<std::string> Us {

  };
  std::vector<std::string> UJs {
    "jal"
  };

  // contains a list of valid instructions for each format so that premature matches to
  // a different format are rejected
  std::unordered_map<ParseFmt, std::vector<std::string>> validInstructions {
    {ParseFmt::R,  Rs},
    {ParseFmt::I,  Is},
    {ParseFmt::S,  Ss},
    {ParseFmt::SB, SBs},
    {ParseFmt::U,  Us},
    {ParseFmt::UJ, UJs},
    {ParseFmt::I_, Is},
    {ParseFmt::S_, Ss},
  };

  struct RegexMatchIndex {
    size_t name;
    std::optional<size_t> rs1;
    std::optional<size_t> rs2;
    std::optional<size_t> rd;
    std::optional<size_t> imm;
  };
  std::unordered_map<ParseFmt, RegexMatchIndex> indexLookup {
    {ParseFmt::R,   { .name=1, .rs1=3, .rs2=4, .rd=2, .imm=std::nullopt }},
    {ParseFmt::I,   { .name=1, .rs1=3, .rs2=std::nullopt, .rd=2, .imm=4 }},
    {ParseFmt::S,   { .name=1, .rs1=3, .rs2=2, .rd=std::nullopt, .imm=4 }},
    {ParseFmt::SB,  { .name=1, .rs1=2, .rs2=3, .rd=std::nullopt, .imm=4 }},
    {ParseFmt::U,   { .name=1, .rs1=std::nullopt, .rs2=std::nullopt, .rd=2, .imm=3 }},
    {ParseFmt::UJ,  { .name=1, .rs1=std::nullopt, .rs2=std::nullopt, .rd=2, .imm=3 }},
    {ParseFmt::I_,  { .name=1, .rs1=4, .rs2=std::nullopt, .rd=2, .imm=3 }},
    {ParseFmt::S_,  { .name=1, .rs1=4, .rs2=2, .rd=std::nullopt, .imm=3 }},
  };

  struct VariableBitFields {
    std::optional<uint8_t> rs1;
    std::optional<uint8_t> rs2;
    std::optional<uint8_t> rd;
    std::optional<uint32_t> imm;
  };

  inline void checkRegisterInBounds(uint32_t reg) {
    if (reg > 31) {
      throw std::invalid_argument("invalid register: " + std::to_string(reg));
    }
  }

  ParsedInstruction makeInstruction(
    const std::string &name,
    const FixedBitFields &fFields,
    const VariableBitFields &vFields
  ) {
    // can handle two types together (like S and SB) as long as the set of valid fields is same
    if (contains(Rs, name)) {
      checkRegisterInBounds(vFields.rs1.value());
      checkRegisterInBounds(vFields.rs2.value());
      checkRegisterInBounds(vFields.rd.value());
      return R {
        .opcode = fFields.opcode,
        .rd = vFields.rd.value(),
        .func3 = fFields.func3.value(),
        .rs1 = vFields.rs1.value(),
        .rs2 = vFields.rs2.value(),
        .func7 = fFields.func7.value()
      };
    } else if (contains(Is, name)) {
      checkRegisterInBounds(vFields.rs1.value());
      checkRegisterInBounds(vFields.rd.value());
      return I {
        .opcode = fFields.opcode,
        .rd = vFields.rd.value(),
        .func3 = fFields.func3.value(),
        .rs1 = vFields.rs1.value(),
        .imm = vFields.imm.value()
      };
    } else if (contains(Ss, name) || contains(SBs, name)) {
      checkRegisterInBounds(vFields.rs1.value());
      checkRegisterInBounds(vFields.rs2.value());
      uint32_t lowerImm = extractBits(vFields.imm.value(), 0, 4);
      uint32_t upperImm = extractBits(vFields.imm.value(), 5, 11);
      return S {
        .opcode = fFields.opcode,
        .lowerImm = lowerImm,
        .func3 = fFields.func3.value(),
        .rs1 = vFields.rs1.value(),
        .rs2 = vFields.rs2.value(),
        .upperImm = upperImm
      };
    } else if (contains(Us, name) || contains(UJs, name)) {
      checkRegisterInBounds(vFields.rd.value());
      return U {
        .opcode = fFields.opcode,
        .rd = vFields.rd.value(),
        .imm = vFields.imm.value()
      };
    } else {
      throw std::invalid_argument("cannot find instruction: " + name);
    }
  }

  VariableBitFields makeFieldsFromIndices(
    const std::smatch &matchResults,
    const RegexMatchIndex &indices
  ) {
    return VariableBitFields {
      .rs1 = indices.rs1 ?
        std::optional(std::stoi(matchResults[*indices.rs1])) : std::nullopt,
      .rs2 = indices.rs2 ? 
        std::optional(std::stoi(matchResults[*indices.rs2])) : std::nullopt,
      .rd = indices.rd ? 
        std::optional(std::stoi(matchResults[*indices.rd])) : std::nullopt,
      .imm = indices.imm ? 
        std::optional(std::stoi(matchResults[*indices.imm])) : std::nullopt
    };
  }

  ParsedInstruction parseInstruction(const std::string &line) {
    for (   // iterate over ParseFmts
      ParseFmt fmt = ParseFmt::R;
      char(fmt) < parseFmtCount;
      fmt = static_cast<ParseFmt>(char(fmt) + 1)
    ) {
      std::regex pattern(regexLookup[fmt]);
      std::smatch matchResults;
      // skip if the formats don't even match
      if (!std::regex_match(line, matchResults, pattern)) {
        continue;
      }
      // relies on the fact that instruction capture pos is always 1 in regex-s
      std::string instructionName = matchResults[1];
      // skip if the instruction doesn't have the current format
      if (!contains(validInstructions[fmt], instructionName)) {
        continue;
      }
      FixedBitFields fFields = fixedFieldsLookup[instructionName];
      const RegexMatchIndex &indices = indexLookup[fmt];
      VariableBitFields vFields = makeFieldsFromIndices(matchResults, indices);
      return makeInstruction(instructionName, fFields, vFields);
    }
    throw std::invalid_argument("cannot parse instruction: " + line);
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

std::vector<Word> encodeInstructions(std::istream &is) {
  std::vector<Word> encoded;
  for (std::string line; std::getline(is, line); ) {
    // skip empty lines
    if (line == "") {
      continue;
    }
    encoded.push_back(encodeInstruction(line));
  }
  return encoded;
}

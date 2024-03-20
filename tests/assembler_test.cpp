#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include "assembler.hpp"

namespace {
  TEST(EncodeInstructionTest, Add) {
    Word instruction = encodeInstruction("add x1, x2, x3");
    uint32_t add = 0b0000000'00011'00010'000'00001'0110011;
    EXPECT_EQ(instruction, add);
  }

  TEST(EncodeInstructionTest, Sub) {
    Word instruction = encodeInstruction("sub x1, x2, x3");
    uint32_t sub = 0b0100000'00011'00010'000'00001'0110011;
    EXPECT_EQ(instruction, sub);
  }

  TEST(EncodeInstructionTest, Addi) {
    Word instruction = encodeInstruction("addi x1, x2, 1000");
    uint32_t addi = 0b001111101000'00010'000'00001'0010011;
    EXPECT_EQ(instruction, addi);
  }

  TEST(EncodeInstructionTest, Lw) {
    Word instruction = encodeInstruction("lw x1, x2, 4");
    uint32_t lw = 0b000000000100'00010'010'00001'0000011;
    EXPECT_EQ(instruction, lw);
  }

  TEST(EncodeInstructionTest, Sw) {
    Word instruction = encodeInstruction("sw x1, x2, 4");
    uint32_t sw = 0b0000000'00001'00010'010'00100'0100011;
    EXPECT_EQ(instruction, sw);
  }

  TEST(EncodeInstructionTest, LwBracketed) {
    Word instruction = encodeInstruction("lw x1, 4(x2)");
    uint32_t lw = 0b000000000100'00010'010'00001'0000011;
    EXPECT_EQ(instruction, lw);
  }

  TEST(EncodeInstructionTest, SwBracketed) {
    Word instruction = encodeInstruction("sw x1, 4(x2)");
    uint32_t sw = 0b0000000'00001'00010'010'00100'0100011;
    EXPECT_EQ(instruction, sw);
  }

  TEST(EncodeInstructionSet, Beq) {
    Word instruction = encodeInstruction("beq x1, x2, 4");
    uint32_t beq = 0b0000000'00010'00001'000'00100'1100011;
    EXPECT_EQ(instruction, beq);
  }
}

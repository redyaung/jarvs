#include <gtest/gtest.h>
#include "assembler.hpp"

namespace {
  TEST(EncodeTest, Add) {
    Word instruction = encodeInstruction("add x1, x2, x3");
    uint32_t add = 0b0000000'00011'00010'000'00001'0110011;
    EXPECT_EQ(instruction, add);
  }

  TEST(EncodeTest, Sub) {
    Word instruction = encodeInstruction("sub x1, x2, x3");
    uint32_t sub = 0b0100000'00011'00010'000'00001'0110011;
    EXPECT_EQ(instruction, sub);
  }

  TEST(EncodeTest, Addi) {
    Word instruction = encodeInstruction("addi x1, x2, 1000");
    uint32_t addi = 0b001111101000'00010'000'00001'0010011;
    EXPECT_EQ(instruction, addi);
  }

  TEST(EncodeTest, Lw) {
    Word instruction = encodeInstruction("lw x1, x2, 4");
    uint32_t lw = 0b000000000100'00010'010'00001'0000011;
    EXPECT_EQ(instruction, lw);
  }

  TEST(EncodeTest, Sw) {
    Word instruction = encodeInstruction("sw x1, x2, 4");
    uint32_t sw = 0b0000000'00001'00010'010'00100'0100011;
    EXPECT_EQ(instruction, sw);
  }
}

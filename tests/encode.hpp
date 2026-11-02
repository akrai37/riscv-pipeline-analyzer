#pragma once

// Instruction encoders for building small test programs in C++ without a
// cross toolchain. decoder_test.cpp checks them against llvm-mc encodings.

#include <cstdint>

namespace enc {

// ABI register numbers.
// clang-format off
enum Reg : unsigned {
  zero, ra, sp, gp, tp, t0, t1, t2, s0, s1, a0, a1, a2, a3, a4, a5,
  a6, a7, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, t3, t4, t5, t6,
};
// clang-format on

constexpr uint32_t r_type(uint32_t f7, unsigned rs2, unsigned rs1, uint32_t f3, unsigned rd,
                          uint32_t opc) {
  return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | opc;
}

constexpr uint32_t i_type(int32_t imm, unsigned rs1, uint32_t f3, unsigned rd, uint32_t opc) {
  return (static_cast<uint32_t>(imm) & 0xfff) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | opc;
}

constexpr uint32_t s_type(int32_t imm, unsigned rs2, unsigned rs1, uint32_t f3) {
  const auto u = static_cast<uint32_t>(imm);
  return ((u >> 5) & 0x7f) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (u & 0x1f) << 7 | 0x23;
}

constexpr uint32_t b_type(int32_t imm, unsigned rs2, unsigned rs1, uint32_t f3) {
  const auto u = static_cast<uint32_t>(imm);
  return ((u >> 12) & 1) << 31 | ((u >> 5) & 0x3f) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 |
         ((u >> 1) & 0xf) << 8 | ((u >> 11) & 1) << 7 | 0x63;
}

constexpr uint32_t j_type(int32_t imm, unsigned rd) {
  const auto u = static_cast<uint32_t>(imm);
  return ((u >> 20) & 1) << 31 | ((u >> 1) & 0x3ff) << 21 | ((u >> 11) & 1) << 20 |
         ((u >> 12) & 0xff) << 12 | rd << 7 | 0x6f;
}

// `upper` is the 20-bit value placed in bits 31:12, as written in assembly.
constexpr uint32_t lui(unsigned rd, uint32_t upper) {
  return upper << 12 | rd << 7 | 0x37;
}
constexpr uint32_t auipc(unsigned rd, uint32_t upper) {
  return upper << 12 | rd << 7 | 0x17;
}
constexpr uint32_t jal(unsigned rd, int32_t off) {
  return j_type(off, rd);
}
constexpr uint32_t jalr(unsigned rd, unsigned rs1, int32_t off) {
  return i_type(off, rs1, 0, rd, 0x67);
}

constexpr uint32_t beq(unsigned rs1, unsigned rs2, int32_t off) {
  return b_type(off, rs2, rs1, 0);
}
constexpr uint32_t bne(unsigned rs1, unsigned rs2, int32_t off) {
  return b_type(off, rs2, rs1, 1);
}
constexpr uint32_t blt(unsigned rs1, unsigned rs2, int32_t off) {
  return b_type(off, rs2, rs1, 4);
}
constexpr uint32_t bge(unsigned rs1, unsigned rs2, int32_t off) {
  return b_type(off, rs2, rs1, 5);
}
constexpr uint32_t bltu(unsigned rs1, unsigned rs2, int32_t off) {
  return b_type(off, rs2, rs1, 6);
}

constexpr uint32_t lb(unsigned rd, unsigned rs1, int32_t off) {
  return i_type(off, rs1, 0, rd, 3);
}
constexpr uint32_t lh(unsigned rd, unsigned rs1, int32_t off) {
  return i_type(off, rs1, 1, rd, 3);
}
constexpr uint32_t lw(unsigned rd, unsigned rs1, int32_t off) {
  return i_type(off, rs1, 2, rd, 3);
}
constexpr uint32_t lbu(unsigned rd, unsigned rs1, int32_t off) {
  return i_type(off, rs1, 4, rd, 3);
}
constexpr uint32_t lhu(unsigned rd, unsigned rs1, int32_t off) {
  return i_type(off, rs1, 5, rd, 3);
}
constexpr uint32_t sb(unsigned rs2, unsigned rs1, int32_t off) {
  return s_type(off, rs2, rs1, 0);
}
constexpr uint32_t sh(unsigned rs2, unsigned rs1, int32_t off) {
  return s_type(off, rs2, rs1, 1);
}
constexpr uint32_t sw(unsigned rs2, unsigned rs1, int32_t off) {
  return s_type(off, rs2, rs1, 2);
}

constexpr uint32_t addi(unsigned rd, unsigned rs1, int32_t imm) {
  return i_type(imm, rs1, 0, rd, 0x13);
}
constexpr uint32_t slli(unsigned rd, unsigned rs1, unsigned sh) {
  return r_type(0x00, sh, rs1, 1, rd, 0x13);
}
constexpr uint32_t srai(unsigned rd, unsigned rs1, unsigned sh) {
  return r_type(0x20, sh, rs1, 5, rd, 0x13);
}

constexpr uint32_t add(unsigned rd, unsigned rs1, unsigned rs2) {
  return r_type(0x00, rs2, rs1, 0, rd, 0x33);
}
constexpr uint32_t sub(unsigned rd, unsigned rs1, unsigned rs2) {
  return r_type(0x20, rs2, rs1, 0, rd, 0x33);
}
constexpr uint32_t mul(unsigned rd, unsigned rs1, unsigned rs2) {
  return r_type(0x01, rs2, rs1, 0, rd, 0x33);
}
constexpr uint32_t div(unsigned rd, unsigned rs1, unsigned rs2) {
  return r_type(0x01, rs2, rs1, 4, rd, 0x33);
}

constexpr uint32_t ecall() {
  return 0x00000073;
}
constexpr uint32_t ebreak() {
  return 0x00100073;
}
constexpr uint32_t nop() {
  return addi(zero, zero, 0);
}

}  // namespace enc

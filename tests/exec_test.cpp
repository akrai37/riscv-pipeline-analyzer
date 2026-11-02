#include "rvsim/exec.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include "encode.hpp"
#include "rvsim/isa.hpp"

namespace {

using rvsim::compute;
using rvsim::decode;
using rvsim::Op;

constexpr uint32_t kIntMin = 0x80000000u;
constexpr uint32_t kMinus1 = 0xffffffffu;

uint32_t rr(uint32_t raw, uint32_t a, uint32_t b) {
  return compute(decode(raw), 0, a, b);
}

// R-type ops by mnemonic, operands in a1/a2, so tests read like the spec.
uint32_t op(Op o, uint32_t a, uint32_t b) {
  rvsim::Instr in;
  in.op = o;
  return compute(in, 0, a, b);
}

TEST(Exec, AddSubWrapAround) {
  EXPECT_EQ(op(Op::Add, 0x7fffffffu, 1), kIntMin);
  EXPECT_EQ(op(Op::Sub, 0, 1), kMinus1);
}

TEST(Exec, SignedVersusUnsignedCompare) {
  EXPECT_EQ(op(Op::Slt, kMinus1, 1), 1u);
  EXPECT_EQ(op(Op::Sltu, kMinus1, 1), 0u);
  EXPECT_EQ(op(Op::Slt, 1, 1), 0u);
  // sltiu compares against the sign-extended immediate as unsigned, so
  // `sltiu rd, rs1, -1` is true for every rs1 except 0xffffffff.
  EXPECT_EQ(rr(enc::i_type(-1, enc::a1, 3, enc::a0, 0x13), 5, 0), 1u);
  EXPECT_EQ(rr(enc::i_type(-1, enc::a1, 3, enc::a0, 0x13), kMinus1, 0), 0u);
}

TEST(Exec, ShiftsUseLowFiveBits) {
  EXPECT_EQ(op(Op::Sll, 1, 33), 2u);
  EXPECT_EQ(op(Op::Srl, kIntMin, 31), 1u);
  EXPECT_EQ(op(Op::Sra, kIntMin, 31), kMinus1);
  EXPECT_EQ(op(Op::Sra, 0x40000000u, 30), 1u);
  EXPECT_EQ(rr(enc::srai(enc::a0, enc::a1, 4), 0xf0000000u, 0), 0xff000000u);
  EXPECT_EQ(rr(enc::slli(enc::a0, enc::a1, 31), 3, 0), kIntMin);
}

TEST(Exec, MultiplyHighHalves) {
  EXPECT_EQ(op(Op::Mul, 0x10000u, 0x10000u), 0u);
  EXPECT_EQ(op(Op::Mul, kMinus1, kMinus1), 1u);
  EXPECT_EQ(op(Op::Mulh, kMinus1, kMinus1), 0u);             // (-1)(-1) = 1
  EXPECT_EQ(op(Op::Mulh, kIntMin, kIntMin), 0x40000000u);    // 2^62
  EXPECT_EQ(op(Op::Mulh, kIntMin, 1), kMinus1);              // sign of -2^31
  EXPECT_EQ(op(Op::Mulhu, kMinus1, kMinus1), 0xfffffffeu);   // (2^32-1)^2
  EXPECT_EQ(op(Op::Mulhsu, kMinus1, kMinus1), kMinus1);      // -1 * (2^32-1)
  EXPECT_EQ(op(Op::Mulhsu, kIntMin, kMinus1), 0x80000000u);  // -2^31 * (2^32-1)
  EXPECT_EQ(op(Op::Mulhsu, 1, kMinus1), 0u);
}

TEST(Exec, DivisionByZeroHasDefinedResults) {
  EXPECT_EQ(op(Op::Div, 7, 0), kMinus1);
  EXPECT_EQ(op(Op::Divu, 7, 0), kMinus1);
  EXPECT_EQ(op(Op::Rem, 7, 0), 7u);
  EXPECT_EQ(op(Op::Remu, 7, 0), 7u);
}

TEST(Exec, SignedDivisionOverflow) {
  EXPECT_EQ(op(Op::Div, kIntMin, kMinus1), kIntMin);
  EXPECT_EQ(op(Op::Rem, kIntMin, kMinus1), 0u);
}

TEST(Exec, DivisionRoundsTowardZero) {
  EXPECT_EQ(op(Op::Div, static_cast<uint32_t>(-7), 2), static_cast<uint32_t>(-3));
  EXPECT_EQ(op(Op::Rem, static_cast<uint32_t>(-7), 2), static_cast<uint32_t>(-1));
  EXPECT_EQ(op(Op::Divu, static_cast<uint32_t>(-7), 2), 0x7ffffffcu);
  EXPECT_EQ(op(Op::Remu, static_cast<uint32_t>(-7), 2), 1u);
}

TEST(Exec, UpperImmediatesAndLinks) {
  EXPECT_EQ(compute(decode(enc::lui(enc::a0, 0xfffff)), 0x100, 0, 0), 0xfffff000u);
  EXPECT_EQ(compute(decode(enc::auipc(enc::a0, 0x1)), 0x100, 0, 0), 0x1100u);
  EXPECT_EQ(compute(decode(enc::jal(enc::ra, 64)), 0x100, 0, 0), 0x104u);
  EXPECT_EQ(compute(decode(enc::jalr(enc::ra, enc::t0, 0)), 0x100, 0, 0), 0x104u);
}

TEST(Exec, ControlTargets) {
  EXPECT_EQ(rvsim::control_target(decode(enc::jal(enc::ra, -8)), 0x100, 0), 0xf8u);
  EXPECT_EQ(rvsim::control_target(decode(enc::beq(enc::a0, enc::a1, 12)), 0x100, 0), 0x10cu);
  // jalr clears bit 0 of the computed target.
  EXPECT_EQ(rvsim::control_target(decode(enc::jalr(enc::ra, enc::t0, 3)), 0, 0x200), 0x202u);
}

TEST(Exec, BranchConditions) {
  EXPECT_TRUE(rvsim::branch_taken(Op::Beq, 5, 5));
  EXPECT_FALSE(rvsim::branch_taken(Op::Bne, 5, 5));
  EXPECT_TRUE(rvsim::branch_taken(Op::Blt, kMinus1, 0));
  EXPECT_FALSE(rvsim::branch_taken(Op::Bltu, kMinus1, 0));
  EXPECT_TRUE(rvsim::branch_taken(Op::Bge, 0, kMinus1));
  EXPECT_FALSE(rvsim::branch_taken(Op::Bgeu, 0, kMinus1));
  EXPECT_TRUE(rvsim::branch_taken(Op::Bge, 3, 3));
  EXPECT_TRUE(rvsim::branch_taken(Op::Bgeu, 3, 3));
}

TEST(Exec, LoadExtension) {
  EXPECT_EQ(rvsim::extend_load(Op::Lb, 0x80), 0xffffff80u);
  EXPECT_EQ(rvsim::extend_load(Op::Lbu, 0x80), 0x80u);
  EXPECT_EQ(rvsim::extend_load(Op::Lh, 0x8000), 0xffff8000u);
  EXPECT_EQ(rvsim::extend_load(Op::Lhu, 0x8000), 0x8000u);
  EXPECT_EQ(rvsim::extend_load(Op::Lw, 0x80000000u), 0x80000000u);
  EXPECT_EQ(rvsim::access_size(Op::Lhu), 2u);
  EXPECT_EQ(rvsim::access_size(Op::Sw), 4u);
  EXPECT_EQ(rvsim::access_size(Op::Add), 0u);
}

TEST(Exec, MemoryAddressUsesSignedOffset) {
  EXPECT_EQ(rvsim::mem_address(decode(enc::lw(enc::a0, enc::sp, -4)), 0x1000), 0xffcu);
  EXPECT_EQ(rvsim::mem_address(decode(enc::sw(enc::a0, enc::sp, 2047)), 0x1000), 0x17ffu);
}

}  // namespace

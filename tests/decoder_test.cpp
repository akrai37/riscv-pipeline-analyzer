#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "encode.hpp"
#include "rvsim/isa.hpp"

namespace {

using rvsim::decode;
using rvsim::Instr;
using rvsim::Op;

struct Golden {
  uint32_t raw;
  Op op;
  unsigned rd, rs1, rs2;
  int32_t imm;
  const char* text;
};

// Encodings produced by `llvm-mc -triple=riscv32 -mattr=+m -show-encoding`
// from the assembly in the last column. Immediates cover sign boundaries
// for every format.
constexpr Golden kGolden[] = {
    {0xfffff537, Op::Lui, 10, 0, 0, -4096, "lui a0, 0xfffff"},
    {0x12345317, Op::Auipc, 6, 0, 0, 305418240, "auipc t1, 0x12345"},
    {0x801ff0ef, Op::Jal, 1, 0, 0, -2048, "jal ra, -2048"},
    {0x7ffff06f, Op::Jal, 0, 0, 0, 1048574, "jal zero, 1048574"},
    {0xffc280e7, Op::Jalr, 1, 5, 0, -4, "jalr ra, -4(t0)"},
    {0x80b50063, Op::Beq, 0, 10, 11, -4096, "beq a0, a1, -4096"},
    {0x7e941fe3, Op::Bne, 0, 8, 9, 4094, "bne s0, s1, 4094"},
    {0x0062c863, Op::Blt, 0, 5, 6, 16, "blt t0, t1, 16"},
    {0xffc3dfe3, Op::Bge, 0, 7, 28, -2, "bge t2, t3, -2"},
    {0x00d66463, Op::Bltu, 0, 12, 13, 8, "bltu a2, a3, 8"},
    {0xfef77ce3, Op::Bgeu, 0, 14, 15, -8, "bgeu a4, a5, -8"},
    {0x80010503, Op::Lb, 10, 2, 0, -2048, "lb a0, -2048(sp)"},
    {0x7ff11583, Op::Lh, 11, 2, 0, 2047, "lh a1, 2047(sp)"},
    {0x0001a603, Op::Lw, 12, 3, 0, 0, "lw a2, 0(gp)"},
    {0xfff24683, Op::Lbu, 13, 4, 0, -1, "lbu a3, -1(tp)"},
    {0x0062d703, Op::Lhu, 14, 5, 0, 6, "lhu a4, 6(t0)"},
    {0xfef40fa3, Op::Sb, 0, 8, 15, -1, "sb a5, -1(s0)"},
    {0x7f049fa3, Op::Sh, 0, 9, 16, 2047, "sh a6, 2047(s1)"},
    {0x81192023, Op::Sw, 0, 18, 17, -2048, "sw a7, -2048(s2)"},
    {0xff010113, Op::Addi, 2, 2, 0, -16, "addi sp, sp, -16"},
    {0xfffa2993, Op::Slti, 19, 20, 0, -1, "slti s3, s4, -1"},
    {0x7ffb3a93, Op::Sltiu, 21, 22, 0, 2047, "sltiu s5, s6, 2047"},
    {0x800c4b93, Op::Xori, 23, 24, 0, -2048, "xori s7, s8, -2048"},
    {0x0ffd6c93, Op::Ori, 25, 26, 0, 255, "ori s9, s10, 255"},
    {0x00fe7d93, Op::Andi, 27, 28, 0, 15, "andi s11, t3, 15"},
    {0x01ff1e93, Op::Slli, 29, 30, 0, 31, "slli t4, t5, 31"},
    {0x0012df93, Op::Srli, 31, 5, 0, 1, "srli t6, t0, 1"},
    {0x41f3d313, Op::Srai, 6, 7, 0, 31, "srai t1, t2, 31"},
    {0x00c58533, Op::Add, 10, 11, 12, 0, "add a0, a1, a2"},
    {0x40f706b3, Op::Sub, 13, 14, 15, 0, "sub a3, a4, a5"},
    {0x00889833, Op::Sll, 16, 17, 8, 0, "sll a6, a7, s0"},
    {0x013924b3, Op::Slt, 9, 18, 19, 0, "slt s1, s2, s3"},
    {0x016aba33, Op::Sltu, 20, 21, 22, 0, "sltu s4, s5, s6"},
    {0x019c4bb3, Op::Xor, 23, 24, 25, 0, "xor s7, s8, s9"},
    {0x01cddd33, Op::Srl, 26, 27, 28, 0, "srl s10, s11, t3"},
    {0x41ff5eb3, Op::Sra, 29, 30, 31, 0, "sra t4, t5, t6"},
    {0x003160b3, Op::Or, 1, 2, 3, 0, "or ra, sp, gp"},
    {0x0062f233, Op::And, 4, 5, 6, 0, "and tp, t0, t1"},
    {0x0ff0000f, Op::Fence, 0, 0, 0, 0, "fence"},
    {0x00000073, Op::Ecall, 0, 0, 0, 0, "ecall"},
    {0x00100073, Op::Ebreak, 0, 0, 0, 0, "ebreak"},
    {0x02c58533, Op::Mul, 10, 11, 12, 0, "mul a0, a1, a2"},
    {0x02f716b3, Op::Mulh, 13, 14, 15, 0, "mulh a3, a4, a5"},
    {0x0288a833, Op::Mulhsu, 16, 17, 8, 0, "mulhsu a6, a7, s0"},
    {0x033934b3, Op::Mulhu, 9, 18, 19, 0, "mulhu s1, s2, s3"},
    {0x036aca33, Op::Div, 20, 21, 22, 0, "div s4, s5, s6"},
    {0x039c5bb3, Op::Divu, 23, 24, 25, 0, "divu s7, s8, s9"},
    {0x03cded33, Op::Rem, 26, 27, 28, 0, "rem s10, s11, t3"},
    {0x03ff7eb3, Op::Remu, 29, 30, 31, 0, "remu t4, t5, t6"},
};

std::string mnemonic(const char* text) {
  const std::string s(text);
  return s.substr(0, s.find(' '));
}

class GoldenDecode : public ::testing::TestWithParam<Golden> {};

TEST_P(GoldenDecode, MatchesAssembler) {
  const Golden& g = GetParam();
  const Instr in = decode(g.raw);
  EXPECT_EQ(in.op, g.op) << g.text;
  EXPECT_EQ(in.rd, g.rd) << g.text;
  EXPECT_EQ(in.rs1, g.rs1) << g.text;
  EXPECT_EQ(in.rs2, g.rs2) << g.text;
  EXPECT_EQ(in.imm, g.imm) << g.text;
  EXPECT_EQ(in.raw, g.raw);
  EXPECT_EQ(rvsim::op_name(in.op), mnemonic(g.text));
}

INSTANTIATE_TEST_SUITE_P(AllOps, GoldenDecode, ::testing::ValuesIn(kGolden),
                         [](const ::testing::TestParamInfo<Golden>& info) {
                           return mnemonic(info.param.text) + "_" + std::to_string(info.index);
                         });

TEST(Decoder, CoversEveryOp) {
  bool seen[static_cast<int>(Op::Remu) + 1] = {};
  for (const Golden& g : kGolden) {
    seen[static_cast<int>(g.op)] = true;
  }
  for (int op = static_cast<int>(Op::Lui); op <= static_cast<int>(Op::Remu); ++op) {
    EXPECT_TRUE(seen[op]) << "no golden encoding for " << rvsim::op_name(static_cast<Op>(op));
  }
}

TEST(Decoder, RejectsReservedAndUnsupportedEncodings) {
  const uint32_t illegal[] = {
      0x00000000,  // all-zero word is defined illegal
      0xffffffff,
      0x00004501,  // compressed (c.li); the C extension is not implemented
      0x34011073,  // csrrw: CSRs are out of scope
      0x30200073,  // mret
      0x0000100f,  // fence.i
      0x00002063,  // branch funct3 = 2
      0x0000b003,  // ld (RV64 only)
      0x0000e023,  // store funct3 = 6
      0x02009093,  // slli with shamt bit 5 set (RV64 only)
      0x80000033,  // OP with unknown funct7
      0x40001033,  // OP funct7 = 0x20 with funct3 = 1
      0x00001067,  // jalr funct3 = 1
  };
  for (const uint32_t raw : illegal) {
    EXPECT_EQ(decode(raw).op, Op::Illegal) << std::hex << raw;
  }
}

TEST(Decoder, UnusedRegisterFieldsAreZero) {
  // addi a0, a1, -1: bits 24:20 are immediate bits (all ones), not rs2.
  const Instr addi = decode(0xfff58513);
  EXPECT_EQ(addi.op, Op::Addi);
  EXPECT_EQ(addi.rs2, 0);
  // sw a7, -2048(s2): bits 11:7 are immediate bits, not rd.
  const Instr sw = decode(0x81192023);
  EXPECT_EQ(sw.rd, 0);
  // beq: no rd either.
  const Instr beq = decode(0x80b50063);
  EXPECT_EQ(beq.rd, 0);
}

TEST(Decoder, OperandQueries) {
  EXPECT_TRUE(rvsim::reads_rs1(Op::Jalr));
  EXPECT_FALSE(rvsim::reads_rs2(Op::Jalr));
  EXPECT_TRUE(rvsim::writes_rd(Op::Jal));
  EXPECT_FALSE(rvsim::reads_rs1(Op::Jal));
  EXPECT_FALSE(rvsim::writes_rd(Op::Sw));
  EXPECT_TRUE(rvsim::reads_rs2(Op::Sw));
  EXPECT_FALSE(rvsim::writes_rd(Op::Beq));
  EXPECT_FALSE(rvsim::reads_rs1(Op::Lui));
  EXPECT_TRUE(rvsim::is_load(Op::Lhu));
  EXPECT_TRUE(rvsim::is_store(Op::Sb));
  EXPECT_TRUE(rvsim::is_branch(Op::Bgeu));
  EXPECT_TRUE(rvsim::is_jump(Op::Jalr));
  EXPECT_TRUE(rvsim::is_mul(Op::Mulhsu));
  EXPECT_TRUE(rvsim::is_div(Op::Remu));
  EXPECT_FALSE(rvsim::is_div(Op::Mul));
}

TEST(Decoder, Disassembly) {
  EXPECT_EQ(rvsim::disassemble(decode(0xff010113)), "addi sp, sp, -16");
  EXPECT_EQ(rvsim::disassemble(decode(0x80010503)), "lb a0, -2048(sp)");
  EXPECT_EQ(rvsim::disassemble(decode(0x81192023)), "sw a7, -2048(s2)");
  EXPECT_EQ(rvsim::disassemble(decode(0x80b50063)), "beq a0, a1, -4096");
  EXPECT_EQ(rvsim::disassemble(decode(0x12345317)), "auipc t1, 0x12345");
  EXPECT_EQ(rvsim::disassemble(decode(0x02c58533)), "mul a0, a1, a2");
  EXPECT_EQ(rvsim::disassemble(decode(0xffc280e7)), "jalr ra, -4(t0)");
  EXPECT_EQ(rvsim::disassemble(decode(0x00000073)), "ecall");
  EXPECT_EQ(rvsim::disassemble(decode(0x00000000)), "illegal 0x00000000");
}

// The hand-written encoders used by other tests must agree with the assembler.
TEST(Encoder, AgreesWithAssembler) {
  using namespace enc;
  EXPECT_EQ(enc::lui(a0, 0xfffff), 0xfffff537u);
  EXPECT_EQ(enc::auipc(t1, 0x12345), 0x12345317u);
  EXPECT_EQ(enc::jal(ra, -2048), 0x801ff0efu);
  EXPECT_EQ(enc::jal(zero, 1048574), 0x7ffff06fu);
  EXPECT_EQ(enc::jalr(ra, t0, -4), 0xffc280e7u);
  EXPECT_EQ(enc::beq(a0, a1, -4096), 0x80b50063u);
  EXPECT_EQ(enc::bne(s0, s1, 4094), 0x7e941fe3u);
  EXPECT_EQ(enc::blt(t0, t1, 16), 0x0062c863u);
  EXPECT_EQ(enc::bge(t2, t3, -2), 0xffc3dfe3u);
  EXPECT_EQ(enc::bltu(a2, a3, 8), 0x00d66463u);
  EXPECT_EQ(enc::lb(a0, sp, -2048), 0x80010503u);
  EXPECT_EQ(enc::lh(a1, sp, 2047), 0x7ff11583u);
  EXPECT_EQ(enc::lw(a2, gp, 0), 0x0001a603u);
  EXPECT_EQ(enc::lbu(a3, tp, -1), 0xfff24683u);
  EXPECT_EQ(enc::lhu(a4, t0, 6), 0x0062d703u);
  EXPECT_EQ(enc::sb(a5, s0, -1), 0xfef40fa3u);
  EXPECT_EQ(enc::sh(a6, s1, 2047), 0x7f049fa3u);
  EXPECT_EQ(enc::sw(a7, s2, -2048), 0x81192023u);
  EXPECT_EQ(enc::addi(sp, sp, -16), 0xff010113u);
  EXPECT_EQ(enc::slli(t4, t5, 31), 0x01ff1e93u);
  EXPECT_EQ(enc::srai(t1, t2, 31), 0x41f3d313u);
  EXPECT_EQ(enc::add(a0, a1, a2), 0x00c58533u);
  EXPECT_EQ(enc::sub(a3, a4, a5), 0x40f706b3u);
  EXPECT_EQ(enc::mul(a0, a1, a2), 0x02c58533u);
  EXPECT_EQ(enc::div(s4, s5, s6), 0x036aca33u);
}

}  // namespace

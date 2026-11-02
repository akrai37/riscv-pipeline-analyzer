#pragma once

#include <cstdint>
#include <string>

namespace rvsim {

// Every operation the simulator implements: RV32I (minus CSRs and fence.i)
// plus the RV32M multiply/divide extension.
// clang-format off
enum class Op : uint8_t {
  Illegal,
  Lui, Auipc, Jal, Jalr,
  Beq, Bne, Blt, Bge, Bltu, Bgeu,
  Lb, Lh, Lw, Lbu, Lhu,
  Sb, Sh, Sw,
  Addi, Slti, Sltiu, Xori, Ori, Andi, Slli, Srli, Srai,
  Add, Sub, Sll, Slt, Sltu, Xor, Srl, Sra, Or, And,
  Fence, Ecall, Ebreak,
  Mul, Mulh, Mulhsu, Mulhu, Div, Divu, Rem, Remu,
};
// clang-format on

// A decoded instruction. Register fields an encoding does not use are zero,
// so the hazard unit never sees a false dependency on a stray field.
struct Instr {
  Op op = Op::Illegal;
  uint8_t rd = 0;
  uint8_t rs1 = 0;
  uint8_t rs2 = 0;
  int32_t imm = 0;
  uint32_t raw = 0;
};

Instr decode(uint32_t raw);

const char* op_name(Op op);
const char* reg_name(unsigned reg);
std::string disassemble(const Instr& in);

bool reads_rs1(Op op);
bool reads_rs2(Op op);
bool writes_rd(Op op);
bool is_branch(Op op);
bool is_jump(Op op);
bool is_load(Op op);
bool is_store(Op op);
bool is_mul(Op op);
bool is_div(Op op);

}  // namespace rvsim

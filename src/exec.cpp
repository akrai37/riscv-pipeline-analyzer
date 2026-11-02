#include "rvsim/exec.hpp"

#include <cstdint>
#include <limits>

namespace rvsim {

uint32_t compute(const Instr& in, uint32_t pc, uint32_t rs1, uint32_t rs2) {
  const auto imm = static_cast<uint32_t>(in.imm);
  const auto s1 = static_cast<int32_t>(rs1);
  const auto s2 = static_cast<int32_t>(rs2);
  switch (in.op) {
    case Op::Lui:
      return imm;
    case Op::Auipc:
      return pc + imm;
    case Op::Jal:
    case Op::Jalr:
      return pc + 4;

    case Op::Addi:
      return rs1 + imm;
    case Op::Slti:
      return s1 < in.imm ? 1u : 0u;
    case Op::Sltiu:
      return rs1 < imm ? 1u : 0u;
    case Op::Xori:
      return rs1 ^ imm;
    case Op::Ori:
      return rs1 | imm;
    case Op::Andi:
      return rs1 & imm;
    case Op::Slli:
      return rs1 << (imm & 31);
    case Op::Srli:
      return rs1 >> (imm & 31);
    case Op::Srai:
      return static_cast<uint32_t>(s1 >> (imm & 31));

    case Op::Add:
      return rs1 + rs2;
    case Op::Sub:
      return rs1 - rs2;
    case Op::Sll:
      return rs1 << (rs2 & 31);
    case Op::Slt:
      return s1 < s2 ? 1u : 0u;
    case Op::Sltu:
      return rs1 < rs2 ? 1u : 0u;
    case Op::Xor:
      return rs1 ^ rs2;
    case Op::Srl:
      return rs1 >> (rs2 & 31);
    case Op::Sra:
      return static_cast<uint32_t>(s1 >> (rs2 & 31));
    case Op::Or:
      return rs1 | rs2;
    case Op::And:
      return rs1 & rs2;

    case Op::Mul:
      return rs1 * rs2;
    case Op::Mulh:
      return static_cast<uint32_t>((int64_t{s1} * int64_t{s2}) >> 32);
    case Op::Mulhsu:
      return static_cast<uint32_t>((int64_t{s1} * static_cast<int64_t>(rs2)) >> 32);
    case Op::Mulhu:
      return static_cast<uint32_t>((uint64_t{rs1} * uint64_t{rs2}) >> 32);

    // Division never traps in RISC-V: divide-by-zero and signed overflow
    // have defined results (spec, "M" extension, division table).
    case Op::Div:
      if (rs2 == 0) {
        return ~0u;
      }
      if (s1 == std::numeric_limits<int32_t>::min() && s2 == -1) {
        return rs1;
      }
      return static_cast<uint32_t>(s1 / s2);
    case Op::Divu:
      return rs2 == 0 ? ~0u : rs1 / rs2;
    case Op::Rem:
      if (rs2 == 0) {
        return rs1;
      }
      if (s1 == std::numeric_limits<int32_t>::min() && s2 == -1) {
        return 0;
      }
      return static_cast<uint32_t>(s1 % s2);
    case Op::Remu:
      return rs2 == 0 ? rs1 : rs1 % rs2;

    default:
      // Branches, loads, stores and system instructions produce no ALU result.
      return 0;
  }
}

bool branch_taken(Op op, uint32_t rs1, uint32_t rs2) {
  const auto s1 = static_cast<int32_t>(rs1);
  const auto s2 = static_cast<int32_t>(rs2);
  switch (op) {
    case Op::Beq:
      return rs1 == rs2;
    case Op::Bne:
      return rs1 != rs2;
    case Op::Blt:
      return s1 < s2;
    case Op::Bge:
      return s1 >= s2;
    case Op::Bltu:
      return rs1 < rs2;
    case Op::Bgeu:
      return rs1 >= rs2;
    default:
      return false;
  }
}

uint32_t control_target(const Instr& in, uint32_t pc, uint32_t rs1) {
  const auto imm = static_cast<uint32_t>(in.imm);
  if (in.op == Op::Jalr) {
    return (rs1 + imm) & ~1u;
  }
  return pc + imm;
}

uint32_t mem_address(const Instr& in, uint32_t rs1) {
  return rs1 + static_cast<uint32_t>(in.imm);
}

unsigned access_size(Op op) {
  switch (op) {
    case Op::Lb:
    case Op::Lbu:
    case Op::Sb:
      return 1;
    case Op::Lh:
    case Op::Lhu:
    case Op::Sh:
      return 2;
    case Op::Lw:
    case Op::Sw:
      return 4;
    default:
      return 0;
  }
}

uint32_t extend_load(Op op, uint32_t loaded) {
  switch (op) {
    case Op::Lb:
      return static_cast<uint32_t>(int32_t{static_cast<int8_t>(loaded)});
    case Op::Lh:
      return static_cast<uint32_t>(int32_t{static_cast<int16_t>(loaded)});
    case Op::Lbu:
      return loaded & 0xff;
    case Op::Lhu:
      return loaded & 0xffff;
    default:
      return loaded;
  }
}

}  // namespace rvsim

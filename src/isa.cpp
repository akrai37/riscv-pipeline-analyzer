#include "rvsim/isa.hpp"

#include <cstddef>
#include <format>
#include <iterator>

namespace rvsim {
namespace {

enum Flag : uint16_t {
  kReadsRs1 = 1 << 0,
  kReadsRs2 = 1 << 1,
  kWritesRd = 1 << 2,
  kBranch = 1 << 3,
  kJump = 1 << 4,
  kLoad = 1 << 5,
  kStore = 1 << 6,
  kMul = 1 << 7,
  kDiv = 1 << 8,
};

// Operand layout, used to pick the immediate and to format disassembly.
enum class Fmt : uint8_t { None, R, I, Shift, Load, Jalr, S, B, U, J };

struct OpInfo {
  const char* name;
  Fmt fmt;
  uint16_t flags;
};

constexpr uint16_t kRrr = kWritesRd | kReadsRs1 | kReadsRs2;
constexpr uint16_t kRri = kWritesRd | kReadsRs1;

// Indexed by Op; the static_assert below keeps it in sync with the enum.
constexpr OpInfo kOps[] = {
    {"illegal", Fmt::None, 0},
    {"lui", Fmt::U, kWritesRd},
    {"auipc", Fmt::U, kWritesRd},
    {"jal", Fmt::J, kWritesRd | kJump},
    {"jalr", Fmt::Jalr, kRri | kJump},
    {"beq", Fmt::B, kReadsRs1 | kReadsRs2 | kBranch},
    {"bne", Fmt::B, kReadsRs1 | kReadsRs2 | kBranch},
    {"blt", Fmt::B, kReadsRs1 | kReadsRs2 | kBranch},
    {"bge", Fmt::B, kReadsRs1 | kReadsRs2 | kBranch},
    {"bltu", Fmt::B, kReadsRs1 | kReadsRs2 | kBranch},
    {"bgeu", Fmt::B, kReadsRs1 | kReadsRs2 | kBranch},
    {"lb", Fmt::Load, kRri | kLoad},
    {"lh", Fmt::Load, kRri | kLoad},
    {"lw", Fmt::Load, kRri | kLoad},
    {"lbu", Fmt::Load, kRri | kLoad},
    {"lhu", Fmt::Load, kRri | kLoad},
    {"sb", Fmt::S, kReadsRs1 | kReadsRs2 | kStore},
    {"sh", Fmt::S, kReadsRs1 | kReadsRs2 | kStore},
    {"sw", Fmt::S, kReadsRs1 | kReadsRs2 | kStore},
    {"addi", Fmt::I, kRri},
    {"slti", Fmt::I, kRri},
    {"sltiu", Fmt::I, kRri},
    {"xori", Fmt::I, kRri},
    {"ori", Fmt::I, kRri},
    {"andi", Fmt::I, kRri},
    {"slli", Fmt::Shift, kRri},
    {"srli", Fmt::Shift, kRri},
    {"srai", Fmt::Shift, kRri},
    {"add", Fmt::R, kRrr},
    {"sub", Fmt::R, kRrr},
    {"sll", Fmt::R, kRrr},
    {"slt", Fmt::R, kRrr},
    {"sltu", Fmt::R, kRrr},
    {"xor", Fmt::R, kRrr},
    {"srl", Fmt::R, kRrr},
    {"sra", Fmt::R, kRrr},
    {"or", Fmt::R, kRrr},
    {"and", Fmt::R, kRrr},
    {"fence", Fmt::None, 0},
    {"ecall", Fmt::None, 0},
    {"ebreak", Fmt::None, 0},
    {"mul", Fmt::R, kRrr | kMul},
    {"mulh", Fmt::R, kRrr | kMul},
    {"mulhsu", Fmt::R, kRrr | kMul},
    {"mulhu", Fmt::R, kRrr | kMul},
    {"div", Fmt::R, kRrr | kDiv},
    {"divu", Fmt::R, kRrr | kDiv},
    {"rem", Fmt::R, kRrr | kDiv},
    {"remu", Fmt::R, kRrr | kDiv},
};
static_assert(std::size(kOps) == static_cast<std::size_t>(Op::Remu) + 1);

// clang-format off
constexpr const char* kRegNames[32] = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6",
};
// clang-format on

const OpInfo& info(Op op) {
  return kOps[static_cast<std::size_t>(op)];
}

bool has(Op op, uint16_t flag) {
  return (info(op).flags & flag) != 0;
}

// Sign-extends the low `bits` bits of `value`.
constexpr int32_t sext(uint32_t value, unsigned bits) {
  const uint32_t sign = 1u << (bits - 1);
  value &= (1u << bits) - 1;
  return static_cast<int32_t>((value ^ sign) - sign);
}

// clang-format off
constexpr Op kBranchOps[8] = {Op::Beq, Op::Bne,  Op::Illegal, Op::Illegal,
                              Op::Blt, Op::Bge,  Op::Bltu,    Op::Bgeu};
constexpr Op kLoadOps[8] = {Op::Lb,  Op::Lh,  Op::Lw,      Op::Illegal,
                            Op::Lbu, Op::Lhu, Op::Illegal, Op::Illegal};
constexpr Op kStoreOps[8] = {Op::Sb,      Op::Sh,      Op::Sw,      Op::Illegal,
                             Op::Illegal, Op::Illegal, Op::Illegal, Op::Illegal};
constexpr Op kOpImmOps[8] = {Op::Addi, Op::Illegal, Op::Slti, Op::Sltiu,
                             Op::Xori, Op::Illegal, Op::Ori,  Op::Andi};
constexpr Op kOpOps[8] = {Op::Add, Op::Sll, Op::Slt, Op::Sltu,
                          Op::Xor, Op::Srl, Op::Or,  Op::And};
constexpr Op kMulDivOps[8] = {Op::Mul, Op::Mulh, Op::Mulhsu, Op::Mulhu,
                              Op::Div, Op::Divu, Op::Rem,    Op::Remu};
// clang-format on

Op decode_op(uint32_t raw) {
  const uint32_t opcode = raw & 0x7f;
  const uint32_t funct3 = (raw >> 12) & 0x7;
  const uint32_t funct7 = raw >> 25;
  switch (opcode) {
    case 0x37:
      return Op::Lui;
    case 0x17:
      return Op::Auipc;
    case 0x6f:
      return Op::Jal;
    case 0x67:
      return funct3 == 0 ? Op::Jalr : Op::Illegal;
    case 0x63:
      return kBranchOps[funct3];
    case 0x03:
      return kLoadOps[funct3];
    case 0x23:
      return kStoreOps[funct3];
    case 0x13:
      // On RV32 a shift amount with bit 5 set is reserved, which the
      // funct7 checks reject along with any other non-zero upper bits.
      if (funct3 == 1) {
        return funct7 == 0x00 ? Op::Slli : Op::Illegal;
      }
      if (funct3 == 5) {
        if (funct7 == 0x00) {
          return Op::Srli;
        }
        return funct7 == 0x20 ? Op::Srai : Op::Illegal;
      }
      return kOpImmOps[funct3];
    case 0x33:
      if (funct7 == 0x00) {
        return kOpOps[funct3];
      }
      if (funct7 == 0x01) {
        return kMulDivOps[funct3];
      }
      if (funct7 == 0x20 && funct3 == 0) {
        return Op::Sub;
      }
      if (funct7 == 0x20 && funct3 == 5) {
        return Op::Sra;
      }
      return Op::Illegal;
    case 0x0f:
      // fence.i (funct3 = 1) needs instruction-cache coherence, which the
      // split-port memory does not model, so it is rejected.
      return funct3 == 0 ? Op::Fence : Op::Illegal;
    case 0x73:
      // Only ecall and ebreak; CSR access and privileged returns are out of scope.
      if (raw == 0x00000073) {
        return Op::Ecall;
      }
      return raw == 0x00100073 ? Op::Ebreak : Op::Illegal;
    default:
      return Op::Illegal;
  }
}

int32_t decode_imm(Fmt fmt, uint32_t raw) {
  switch (fmt) {
    case Fmt::I:
    case Fmt::Load:
    case Fmt::Jalr:
      return sext(raw >> 20, 12);
    case Fmt::Shift:
      return static_cast<int32_t>((raw >> 20) & 0x1f);
    case Fmt::S:
      return sext(((raw >> 25) << 5) | ((raw >> 7) & 0x1f), 12);
    case Fmt::B:
      return sext(((raw >> 31) & 0x1) << 12 | ((raw >> 7) & 0x1) << 11 | ((raw >> 25) & 0x3f) << 5 |
                      ((raw >> 8) & 0xf) << 1,
                  13);
    case Fmt::U:
      return static_cast<int32_t>(raw & 0xfffff000u);
    case Fmt::J:
      return sext(((raw >> 31) & 0x1) << 20 | ((raw >> 12) & 0xff) << 12 |
                      ((raw >> 20) & 0x1) << 11 | ((raw >> 21) & 0x3ff) << 1,
                  21);
    case Fmt::None:
    case Fmt::R:
      return 0;
  }
  return 0;
}

}  // namespace

Instr decode(uint32_t raw) {
  Instr in;
  in.raw = raw;
  in.op = decode_op(raw);
  if (in.op == Op::Illegal) {
    return in;
  }
  if (has(in.op, kWritesRd)) {
    in.rd = static_cast<uint8_t>((raw >> 7) & 0x1f);
  }
  if (has(in.op, kReadsRs1)) {
    in.rs1 = static_cast<uint8_t>((raw >> 15) & 0x1f);
  }
  if (has(in.op, kReadsRs2)) {
    in.rs2 = static_cast<uint8_t>((raw >> 20) & 0x1f);
  }
  in.imm = decode_imm(info(in.op).fmt, raw);
  return in;
}

const char* op_name(Op op) {
  return info(op).name;
}

const char* reg_name(unsigned reg) {
  return reg < 32 ? kRegNames[reg] : "?";
}

std::string disassemble(const Instr& in) {
  const char* name = op_name(in.op);
  const char* rd = reg_name(in.rd);
  const char* rs1 = reg_name(in.rs1);
  const char* rs2 = reg_name(in.rs2);
  switch (info(in.op).fmt) {
    case Fmt::R:
      return std::format("{} {}, {}, {}", name, rd, rs1, rs2);
    case Fmt::I:
    case Fmt::Shift:
      return std::format("{} {}, {}, {}", name, rd, rs1, in.imm);
    case Fmt::Load:
    case Fmt::Jalr:
      return std::format("{} {}, {}({})", name, rd, in.imm, rs1);
    case Fmt::S:
      return std::format("{} {}, {}({})", name, rs2, in.imm, rs1);
    case Fmt::B:
      return std::format("{} {}, {}, {}", name, rs1, rs2, in.imm);
    case Fmt::U:
      return std::format("{} {}, {:#x}", name, rd, static_cast<uint32_t>(in.imm) >> 12);
    case Fmt::J:
      return std::format("{} {}, {}", name, rd, in.imm);
    case Fmt::None:
      break;
  }
  if (in.op == Op::Illegal) {
    return std::format("illegal {:#010x}", in.raw);
  }
  return name;
}

bool reads_rs1(Op op) {
  return has(op, kReadsRs1);
}
bool reads_rs2(Op op) {
  return has(op, kReadsRs2);
}
bool writes_rd(Op op) {
  return has(op, kWritesRd);
}
bool is_branch(Op op) {
  return has(op, kBranch);
}
bool is_jump(Op op) {
  return has(op, kJump);
}
bool is_load(Op op) {
  return has(op, kLoad);
}
bool is_store(Op op) {
  return has(op, kStore);
}
bool is_mul(Op op) {
  return has(op, kMul);
}
bool is_div(Op op) {
  return has(op, kDiv);
}

}  // namespace rvsim

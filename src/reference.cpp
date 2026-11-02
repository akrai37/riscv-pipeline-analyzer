#include "rvsim/reference.hpp"

#include <format>

#include "rvsim/error.hpp"
#include "rvsim/exec.hpp"
#include "rvsim/isa.hpp"

namespace rvsim {

ReferenceModel::ReferenceModel(Memory& mem, uint32_t entry) : mem_(mem), pc_(entry) {}

void ReferenceModel::set_reg(unsigned r, uint32_t value) {
  if (r != 0) {
    regs_[r] = value;
  }
}

Retired ReferenceModel::step() {
  if (halted()) {
    throw SimError("step after program exit");
  }

  const uint32_t raw = mem_.fetch(pc_);
  const Instr in = decode(raw);
  const uint32_t rs1 = regs_[in.rs1];
  const uint32_t rs2 = regs_[in.rs2];

  Retired r;
  r.pc = pc_;
  r.raw = raw;
  uint32_t next_pc = pc_ + 4;
  uint32_t result = 0;
  bool writes = writes_rd(in.op);

  switch (in.op) {
    case Op::Illegal:
      throw SimError(std::format("illegal instruction {:#010x} at pc {:#010x}", raw, pc_));
    case Op::Ebreak:
      throw SimError(std::format("ebreak at pc {:#010x}", pc_));
    case Op::Ecall: {
      const uint32_t a0 = host_.ecall(regs_, mem_);
      if (!host_.exited()) {
        writes = true;
        r.rd = 10;
        result = a0;
      }
      break;
    }
    case Op::Fence:
      break;
    default:
      if (is_load(in.op)) {
        const uint32_t addr = mem_address(in, rs1);
        result = extend_load(in.op, mem_.load(addr, access_size(in.op)));
      } else if (is_store(in.op)) {
        const unsigned size = access_size(in.op);
        const uint32_t addr = mem_address(in, rs1);
        mem_.store(addr, size, rs2);
        r.store_size = static_cast<uint8_t>(size);
        r.store_addr = addr;
        r.store_value = size == 4 ? rs2 : rs2 & ((1u << (8 * size)) - 1);
      } else if (is_branch(in.op)) {
        if (branch_taken(in.op, rs1, rs2)) {
          next_pc = control_target(in, pc_, rs1);
        }
      } else {
        result = compute(in, pc_, rs1, rs2);
        if (is_jump(in.op)) {
          next_pc = control_target(in, pc_, rs1);
        }
      }
      r.rd = in.rd;
      break;
  }

  if (writes && r.rd != 0) {
    regs_[r.rd] = result;
    r.rd_value = result;
  } else {
    r.rd = 0;
  }
  pc_ = next_pc;
  ++retired_;
  return r;
}

uint64_t ReferenceModel::run(uint64_t max_instructions) {
  uint64_t n = 0;
  while (!halted() && n < max_instructions) {
    step();
    ++n;
  }
  return n;
}

}  // namespace rvsim

#include "rvsim/lockstep.hpp"

#include <format>
#include <optional>

#include "rvsim/error.hpp"
#include "rvsim/isa.hpp"

namespace rvsim {

const char* outcome_name(LockstepResult::Outcome outcome) {
  switch (outcome) {
    case LockstepResult::Outcome::Exited:
      return "exited";
    case LockstepResult::Outcome::Faulted:
      return "faulted";
    case LockstepResult::Outcome::Limit:
      return "limit";
    case LockstepResult::Outcome::Diverged:
      return "diverged";
  }
  return "?";
}

std::string format_retired(const Retired& r) {
  std::string s = std::format("pc={:#010x} {:<24}", r.pc, disassemble(decode(r.raw)));
  if (r.rd != 0) {
    s += std::format(" {}={:#010x}", reg_name(r.rd), r.rd_value);
  }
  if (r.store_size != 0) {
    s += std::format(" mem[{:#010x}]={:#x}/{}B", r.store_addr, r.store_value, r.store_size);
  }
  return s;
}

LockstepResult run_lockstep(Pipeline& pipe, ReferenceModel& ref, uint64_t max_cycles) {
  LockstepResult result;
  auto diverged = [&](std::string msg) {
    result.outcome = LockstepResult::Outcome::Diverged;
    result.message = std::format("after {} matching instructions (cycle {}): {}", result.compared,
                                 pipe.stats().cycles, msg);
    return result;
  };

  while (!pipe.halted() && pipe.stats().cycles < max_cycles) {
    std::optional<Retired> got;
    try {
      got = pipe.cycle();
    } catch (const SimError& e) {
      // The pipeline raises a fault when the instruction reaches WB; the
      // reference must fault on that same instruction.
      try {
        const Retired want = ref.step();
        return diverged(std::format("pipeline faulted ({}) but reference retired {}", e.what(),
                                    format_retired(want)));
      } catch (const SimError&) {
        result.outcome = LockstepResult::Outcome::Faulted;
        result.message = e.what();
        return result;
      }
    }
    if (!got) {
      continue;
    }
    Retired want;
    try {
      want = ref.step();
    } catch (const SimError& e) {
      return diverged(std::format("reference faulted ({}) but pipeline retired {}", e.what(),
                                  format_retired(*got)));
    }
    if (*got != want) {
      return diverged(std::format("pipeline {}\n                reference {}", format_retired(*got),
                                  format_retired(want)));
    }
    ++result.compared;
  }

  if (!pipe.halted()) {
    result.outcome = LockstepResult::Outcome::Limit;
    return result;
  }
  if (!ref.halted()) {
    return diverged("pipeline exited but the reference did not");
  }
  if (pipe.exit_code() != ref.exit_code()) {
    return diverged(std::format("exit code {} vs reference {}", pipe.exit_code(), ref.exit_code()));
  }
  if (pipe.host().output() != ref.host().output()) {
    return diverged("program output differs");
  }
  for (unsigned r = 0; r < 32; ++r) {
    if (pipe.regs()[r] != ref.reg(r)) {
      return diverged(std::format("final {} = {:#010x} vs reference {:#010x}", reg_name(r),
                                  pipe.regs()[r], ref.reg(r)));
    }
  }
  result.outcome = LockstepResult::Outcome::Exited;
  return result;
}

}  // namespace rvsim

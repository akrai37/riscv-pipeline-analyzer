#pragma once

#include <cstdint>

#include "rvsim/host.hpp"
#include "rvsim/memory.hpp"

namespace rvsim {

// Architectural effects of one retired instruction. The pipeline emits the
// same record at writeback, and the lockstep checker compares the two
// streams record by record.
struct Retired {
  uint32_t pc = 0;
  uint32_t raw = 0;
  uint8_t rd = 0;  // 0 when no register was written (writes to x0 included)
  uint32_t rd_value = 0;
  uint8_t store_size = 0;  // 0 when the instruction was not a store
  uint32_t store_addr = 0;
  uint32_t store_value = 0;  // truncated to store_size bytes

  bool operator==(const Retired&) const = default;
};

// Sequential reference model: one instruction per step, no notion of time.
// It defines what a correct execution is; the pipeline must retire exactly
// the same sequence of records.
class ReferenceModel {
 public:
  ReferenceModel(Memory& mem, uint32_t entry);

  // Executes one instruction. Throws SimError on illegal instructions,
  // faults, ebreak, or when called after the program exited.
  Retired step();

  // Steps until exit or until `max_instructions` have retired; returns the
  // number retired by this call.
  uint64_t run(uint64_t max_instructions);

  bool halted() const { return host_.exited(); }
  int exit_code() const { return host_.exit_code(); }
  const Host& host() const { return host_; }

  uint32_t pc() const { return pc_; }
  uint32_t reg(unsigned r) const { return regs_[r]; }
  void set_reg(unsigned r, uint32_t value);
  const RegFile& regs() const { return regs_; }
  uint64_t retired() const { return retired_; }

 private:
  Memory& mem_;
  RegFile regs_{};
  uint32_t pc_;
  uint64_t retired_ = 0;
  Host host_;
};

}  // namespace rvsim

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "rvsim/host.hpp"
#include "rvsim/isa.hpp"
#include "rvsim/memory.hpp"
#include "rvsim/predictor.hpp"
#include "rvsim/reference.hpp"

namespace rvsim {

// Why a cycle produced no retired instruction. Every bubble is tagged with a
// cause when it is created and counted when it reaches writeback, so
// `cycles == retired + sum(lost)` holds exactly.
enum class StallCause : uint8_t {
  Fill,              // pipeline start-up
  LoadUse,           // consumer waits one cycle for a load (forwarding on)
  DataHazard,        // consumer waits for writeback (forwarding off)
  MulDiv,            // EX occupied by a multi-cycle multiply or divide
  Serialize,         // ecall waits for older instructions to retire
  BranchMispredict,  // conditional branch: wrong direction or BTB miss when taken
  JalMiss,           // jal not in the BTB
  JalrMispredict,    // jalr (other than a return) to an unpredicted target
  ReturnMispredict,  // return (jalr through x1/x5) to an unpredicted target
  kCount,
};

inline constexpr std::size_t kStallCauses = static_cast<std::size_t>(StallCause::kCount);

const char* stall_name(StallCause cause);

struct PipelineConfig {
  bool forwarding = true;
  unsigned mul_latency = 3;   // cycles in EX for mul/mulh/mulhsu/mulhu
  unsigned div_latency = 32;  // cycles in EX for div/divu/rem/remu
  PredictorConfig predictor;
};

struct PipelineStats {
  uint64_t cycles = 0;
  uint64_t retired = 0;
  std::array<uint64_t, kStallCauses> lost{};

  uint64_t loads = 0;
  uint64_t stores = 0;
  uint64_t muls = 0;
  uint64_t divs = 0;

  // Control flow, counted when resolved in EX.
  uint64_t branches = 0;
  uint64_t branches_taken = 0;
  uint64_t branch_mispredicts = 0;
  uint64_t jals = 0;
  uint64_t jal_misses = 0;
  uint64_t returns = 0;
  uint64_t return_mispredicts = 0;
  uint64_t other_jalrs = 0;
  uint64_t other_jalr_mispredicts = 0;
  uint64_t control_btb_hits = 0;  // control-flow instructions found in the BTB at fetch

  uint64_t lost_cycles(StallCause c) const { return lost[static_cast<std::size_t>(c)]; }
  uint64_t total_lost() const;
  double cpi() const { return retired == 0 ? 0.0 : static_cast<double>(cycles) / retired; }
};

// Cycle-accurate model of a classic in-order 5-stage pipeline:
//
//   IF   fetch at PC; the branch predictor picks the next PC
//   ID   decode, read registers (written by WB in the first half of the
//        cycle), detect hazards; ecall executes here once older
//        instructions have retired
//   EX   ALU, forwarding from EX/MEM and MEM/WB, branch resolution;
//        multiply/divide hold EX for their latency
//   MEM  loads and stores, with MEM/WB -> MEM forwarding of store data
//   WB   register write, retirement
//
// Pipeline registers are double-buffered: every stage computes the next
// latch contents from the current ones, then all latches update together.
// Instruction and data memory are separate ports onto one memory.
//
// Faults (illegal instruction, bad fetch or access, ebreak) travel with the
// instruction and are raised as SimError only if it reaches writeback, so
// wrong-path instructions can never fault a correct program.
class Pipeline {
 public:
  Pipeline(Memory& mem, uint32_t entry, const PipelineConfig& config);

  // Advances one cycle and returns the instruction retired in it, if any.
  std::optional<Retired> cycle();

  // Runs until exit or until `max_cycles` total cycles; returns cycles run.
  uint64_t run(uint64_t max_cycles);

  bool halted() const { return halted_; }
  int exit_code() const { return host_.exit_code(); }
  const Host& host() const { return host_; }
  const RegFile& regs() const { return regs_; }
  const PipelineStats& stats() const { return stats_; }
  const PipelineConfig& config() const { return config_; }

  // Writes one line per cycle showing every stage.
  void set_trace(std::FILE* out) { trace_ = out; }

 private:
  struct Slot {
    bool valid = false;  // false: bubble
    StallCause cause = StallCause::Fill;

    uint32_t pc = 0;
    uint32_t raw = 0;
    Instr in;
    BranchPredictor::Prediction pred;

    uint32_t rs1_val = 0;
    uint32_t rs2_val = 0;
    uint8_t rd = 0;       // destination, set in ID (a0 for an ecall that returns)
    bool writes = false;  // rd != 0
    uint32_t result = 0;  // ALU result, link address, load data or ecall return
    bool executed = false;
    bool exits = false;

    uint32_t mem_addr = 0;
    uint32_t store_value = 0;

    std::string fault;  // non-empty: raise when this instruction reaches WB
  };

  struct Redirect {
    uint32_t pc;
    StallCause cause;
  };

  static Slot bubble(StallCause cause);

  std::optional<Retired> writeback();
  Slot memory_stage() const;
  std::optional<Redirect> execute(Slot& s);
  std::optional<StallCause> hazard(const Slot& s) const;
  void decode(Slot& s);
  uint32_t forward(unsigned reg, uint32_t value) const;
  Slot fetch();
  unsigned latency(Op op) const;

  Memory& mem_;
  PipelineConfig config_;
  BranchPredictor predictor_;
  Host host_;
  RegFile regs_{};
  uint32_t pc_;

  Slot if_id_;
  Slot id_ex_;
  Slot ex_mem_;
  Slot mem_wb_;
  unsigned ex_remaining_ = 0;  // extra EX cycles left for the op in id_ex_

  bool fetch_stopped_ = false;
  bool halted_ = false;
  PipelineStats stats_;
  std::FILE* trace_ = nullptr;
};

}  // namespace rvsim

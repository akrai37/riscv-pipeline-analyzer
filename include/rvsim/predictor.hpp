#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "rvsim/isa.hpp"

namespace rvsim {

// Direction prediction for conditional branches. All three share the BTB for
// targets: a branch can only be predicted taken when the BTB supplies its
// target in IF, so a BTB miss is always predicted not-taken (fall through).
enum class DirectionPredictor : uint8_t {
  NotTaken,  // never taken
  Btfn,      // backward taken, forward not taken (direction from the BTB target)
  TwoBit,    // per-branch 2-bit saturating counters
};

const char* direction_name(DirectionPredictor p);

// Return-address-stack action of a jump, following the hint table in the
// RISC-V unprivileged spec (section 2.5): x1 and x5 are link registers.
enum class RasAction : uint8_t { None, Push, Pop, PopPush };

RasAction ras_action(const Instr& in);

struct PredictorConfig {
  DirectionPredictor direction = DirectionPredictor::TwoBit;
  unsigned btb_entries = 64;   // direct-mapped, full-PC tags; power of two (0 disables)
  unsigned pht_entries = 256;  // 2-bit counters; power of two
  unsigned ras_entries = 8;    // 0 disables the return address stack
};

inline constexpr unsigned kMaxRasEntries = 16;

// Fetch-side branch prediction: a BTB that identifies control-flow
// instructions and their last target before decode, a pattern history table
// for conditional-branch direction, and an optional return address stack.
//
// predict() runs in IF and updates the RAS speculatively; resolve() runs in
// EX with the real outcome and trains the tables. Each prediction carries a
// checkpoint of the RAS from before its own update, so a misprediction can
// restore the stack exactly.
class BranchPredictor {
 public:
  struct Ras {
    std::array<uint32_t, kMaxRasEntries> entries{};
    uint8_t top = 0;
    uint8_t count = 0;
  };

  struct Prediction {
    uint32_t next_pc = 0;
    bool btb_hit = false;
    Ras ras_before;
  };

  explicit BranchPredictor(const PredictorConfig& config);

  const PredictorConfig& config() const { return config_; }

  Prediction predict(uint32_t pc);

  // Trains on the resolved outcome of a control-flow instruction. On a
  // misprediction the RAS is restored to `pred.ras_before` and the
  // instruction's real RAS action is applied.
  void resolve(const Instr& in, uint32_t pc, bool taken, uint32_t target, bool mispredicted,
               const Prediction& pred);

 private:
  enum class Kind : uint8_t { Branch, Jump };

  struct BtbEntry {
    bool valid = false;
    uint32_t tag = 0;
    uint32_t target = 0;
    Kind kind = Kind::Branch;
    RasAction ras = RasAction::None;
  };

  std::size_t btb_index(uint32_t pc) const { return (pc >> 2) & (btb_.size() - 1); }
  std::size_t pht_index(uint32_t pc) const { return (pc >> 2) & (pht_.size() - 1); }
  void push(uint32_t addr);
  bool pop(uint32_t& addr);
  void apply(RasAction action, uint32_t return_addr, uint32_t& predicted);

  PredictorConfig config_;
  std::vector<BtbEntry> btb_;
  std::vector<uint8_t> pht_;
  Ras ras_;
};

}  // namespace rvsim

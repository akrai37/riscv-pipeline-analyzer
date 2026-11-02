#include "rvsim/predictor.hpp"

#include <bit>
#include <stdexcept>

namespace rvsim {
namespace {

bool is_link(unsigned reg) {
  return reg == 1 || reg == 5;
}

constexpr uint8_t kWeaklyNotTaken = 1;

}  // namespace

const char* direction_name(DirectionPredictor p) {
  switch (p) {
    case DirectionPredictor::NotTaken:
      return "not-taken";
    case DirectionPredictor::Btfn:
      return "btfn";
    case DirectionPredictor::TwoBit:
      return "2bit";
  }
  return "?";
}

RasAction ras_action(const Instr& in) {
  if (in.op == Op::Jal) {
    return is_link(in.rd) ? RasAction::Push : RasAction::None;
  }
  if (in.op != Op::Jalr) {
    return RasAction::None;
  }
  const bool rd_link = is_link(in.rd);
  const bool rs1_link = is_link(in.rs1);
  if (rd_link && rs1_link) {
    return in.rd == in.rs1 ? RasAction::Push : RasAction::PopPush;
  }
  if (rd_link) {
    return RasAction::Push;
  }
  return rs1_link ? RasAction::Pop : RasAction::None;
}

BranchPredictor::BranchPredictor(const PredictorConfig& config) : config_(config) {
  if (config.btb_entries != 0 && !std::has_single_bit(config.btb_entries)) {
    throw std::invalid_argument("BTB entries must be a power of two");
  }
  if (config.pht_entries == 0 || !std::has_single_bit(config.pht_entries)) {
    throw std::invalid_argument("PHT entries must be a non-zero power of two");
  }
  if (config.ras_entries > kMaxRasEntries) {
    throw std::invalid_argument("too many RAS entries");
  }
  btb_.resize(config.btb_entries);
  pht_.assign(config.pht_entries, kWeaklyNotTaken);
}

void BranchPredictor::push(uint32_t addr) {
  const unsigned n = config_.ras_entries;
  if (n == 0) {
    return;
  }
  // Circular: on overflow the oldest entry is overwritten.
  ras_.top = static_cast<uint8_t>((ras_.top + 1) % n);
  ras_.entries[ras_.top] = addr;
  if (ras_.count < n) {
    ++ras_.count;
  }
}

bool BranchPredictor::pop(uint32_t& addr) {
  const unsigned n = config_.ras_entries;
  if (n == 0 || ras_.count == 0) {
    return false;
  }
  addr = ras_.entries[ras_.top];
  ras_.top = static_cast<uint8_t>((ras_.top + n - 1) % n);
  --ras_.count;
  return true;
}

void BranchPredictor::apply(RasAction action, uint32_t return_addr, uint32_t& predicted) {
  if (action == RasAction::Pop || action == RasAction::PopPush) {
    pop(predicted);
  }
  if (action == RasAction::Push || action == RasAction::PopPush) {
    push(return_addr);
  }
}

BranchPredictor::Prediction BranchPredictor::predict(uint32_t pc) {
  Prediction p;
  p.next_pc = pc + 4;
  p.ras_before = ras_;
  if (btb_.empty()) {
    return p;
  }
  const BtbEntry& e = btb_[btb_index(pc)];
  if (!e.valid || e.tag != pc) {
    return p;
  }
  p.btb_hit = true;
  if (e.kind == Kind::Branch) {
    bool taken = false;
    switch (config_.direction) {
      case DirectionPredictor::NotTaken:
        break;
      case DirectionPredictor::Btfn:
        taken = e.target < pc;
        break;
      case DirectionPredictor::TwoBit:
        taken = pht_[pht_index(pc)] >= 2;
        break;
    }
    if (taken) {
      p.next_pc = e.target;
    }
    return p;
  }
  // Unconditional jump: predicted taken, to the RAS top for returns when the
  // stack has an entry, otherwise to the last target seen.
  p.next_pc = e.target;
  apply(e.ras, pc + 4, p.next_pc);
  return p;
}

void BranchPredictor::resolve(const Instr& in, uint32_t pc, bool taken, uint32_t target,
                              bool mispredicted, const Prediction& pred) {
  if (is_branch(in.op)) {
    uint8_t& counter = pht_[pht_index(pc)];
    if (taken && counter < 3) {
      ++counter;
    } else if (!taken && counter > 0) {
      --counter;
    }
  }
  // Only taken instructions are allocated: a never-taken branch needs no
  // target and is predicted correctly by falling through.
  if (taken && !btb_.empty()) {
    BtbEntry& e = btb_[btb_index(pc)];
    e.valid = true;
    e.tag = pc;
    e.target = target;
    e.kind = is_branch(in.op) ? Kind::Branch : Kind::Jump;
    e.ras = ras_action(in);
  }
  if (mispredicted) {
    ras_ = pred.ras_before;
    uint32_t unused = 0;
    apply(ras_action(in), pc + 4, unused);
  }
}

}  // namespace rvsim

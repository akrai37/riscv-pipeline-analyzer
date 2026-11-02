#include "rvsim/pipeline.hpp"

#include <format>
#include <numeric>
#include <stdexcept>
#include <string>

#include "rvsim/error.hpp"
#include "rvsim/exec.hpp"

namespace rvsim {

const char* stall_name(StallCause cause) {
  switch (cause) {
    case StallCause::Fill:
      return "fill";
    case StallCause::LoadUse:
      return "load-use";
    case StallCause::DataHazard:
      return "data-hazard";
    case StallCause::MulDiv:
      return "mul/div";
    case StallCause::Serialize:
      return "serialize";
    case StallCause::BranchMispredict:
      return "branch-mispredict";
    case StallCause::JalMiss:
      return "jal-btb-miss";
    case StallCause::JalrMispredict:
      return "jalr-mispredict";
    case StallCause::ReturnMispredict:
      return "return-mispredict";
    case StallCause::kCount:
      break;
  }
  return "?";
}

uint64_t PipelineStats::total_lost() const {
  return std::accumulate(lost.begin(), lost.end(), uint64_t{0});
}

namespace {

constexpr std::size_t kTraceWidth = 24;

std::string describe(bool valid, const Instr& in, StallCause cause,
                     std::size_t width = kTraceWidth) {
  std::string s = valid ? disassemble(in) : std::string("-") + stall_name(cause);
  s.resize(width, ' ');
  return s;
}

// The IF column also shows the fetch PC, so it is wider.
std::string describe_fetch(uint32_t pc, const Instr& in) {
  std::string s = std::format("{:08x} {}", pc, disassemble(in));
  s.resize(kTraceWidth + 9, ' ');
  return s;
}

std::string describe_no_fetch(StallCause cause) {
  return describe(false, Instr{}, cause, kTraceWidth + 9);
}

}  // namespace

Pipeline::Pipeline(Memory& mem, uint32_t entry, const PipelineConfig& config)
    : mem_(mem), config_(config), predictor_(config.predictor), pc_(entry) {
  if (config.mul_latency == 0 || config.div_latency == 0) {
    throw std::invalid_argument("multiply and divide latencies must be at least 1 cycle");
  }
}

Pipeline::Slot Pipeline::bubble(StallCause cause) {
  Slot s;
  s.cause = cause;
  return s;
}

unsigned Pipeline::latency(Op op) const {
  if (is_mul(op)) {
    return config_.mul_latency;
  }
  if (is_div(op)) {
    return config_.div_latency;
  }
  return 1;
}

std::optional<Retired> Pipeline::cycle() {
  if (halted_) {
    throw SimError("cycle after program exit");
  }

  std::string trace_id, trace_ex, trace_mem, trace_wb, trace_if, trace_event;
  if (trace_ != nullptr) {
    trace_id = describe(if_id_.valid, if_id_.in, if_id_.cause);
    trace_ex = describe(id_ex_.valid, id_ex_.in, id_ex_.cause);
    trace_mem = describe(ex_mem_.valid, ex_mem_.in, ex_mem_.cause);
    trace_wb = describe(mem_wb_.valid, mem_wb_.in, mem_wb_.cause);
  }
  auto emit_trace = [&]() {
    if (trace_ != nullptr) {
      std::fprintf(trace_, "%8llu | %s| %s| %s| %s| %s| %s\n",
                   static_cast<unsigned long long>(stats_.cycles), trace_if.c_str(),
                   trace_id.c_str(), trace_ex.c_str(), trace_mem.c_str(), trace_wb.c_str(),
                   trace_event.c_str());
    }
  };

  // Stages run back to front, each reading only the current latches, so the
  // order models a single clock edge. WB first also gives the register file
  // its write-before-read behaviour for ID.
  std::optional<Retired> retired = writeback();
  // Counted only once WB completes: a fault ends the run inside this cycle,
  // so a cycle that neither retires nor loses a slot is never counted and
  // cycles == retired + lost holds for faulting runs too.
  ++stats_.cycles;
  if (halted_) {
    trace_if = describe_no_fetch(StallCause::Serialize);
    trace_event = "exit";
    emit_trace();
    return retired;
  }

  Slot next_mem_wb = memory_stage();

  // EX. A multiply or divide executes on its first cycle in EX and then
  // holds the stage, freezing ID and IF, until its latency has elapsed.
  Slot ex = id_ex_;
  Slot next_ex_mem = ex;
  std::optional<Redirect> redirect;
  bool ex_busy = false;
  if (ex.valid) {
    if (!ex.executed) {
      redirect = execute(ex);
      ex.executed = true;
      ex_remaining_ = ex.fault.empty() ? latency(ex.in.op) - 1 : 0;
    } else {
      --ex_remaining_;
    }
    ex_busy = ex_remaining_ > 0;
    next_ex_mem = ex_busy ? bubble(StallCause::MulDiv) : ex;
  }

  Slot next_id_ex;
  Slot next_if_id;
  if (ex_busy) {
    next_id_ex = ex;
    next_if_id = if_id_;
    trace_if = describe_no_fetch(StallCause::MulDiv);
    trace_event = std::format("EX busy ({} left)", ex_remaining_);
  } else if (redirect) {
    // Resolved in EX: the instructions in ID and IF are on the wrong path.
    next_id_ex = bubble(redirect->cause);
    next_if_id = bubble(redirect->cause);
    pc_ = redirect->pc;
    trace_if = describe_no_fetch(redirect->cause);
    trace_event = std::format("flush, redirect to {:#010x}", redirect->pc);
  } else {
    Slot id = if_id_;
    const std::optional<StallCause> stall = id.valid ? hazard(id) : std::nullopt;
    if (stall) {
      next_id_ex = bubble(*stall);
      next_if_id = if_id_;
      trace_if = describe_no_fetch(*stall);
      trace_event = std::string("stall: ") + stall_name(*stall);
    } else {
      if (id.valid) {
        decode(id);
      }
      next_id_ex = id;
      if (fetch_stopped_) {
        next_if_id = bubble(StallCause::Serialize);
        trace_if = describe_no_fetch(StallCause::Serialize);
      } else {
        next_if_id = fetch();
        if (trace_ != nullptr) {
          trace_if = describe_fetch(next_if_id.pc, next_if_id.in);
        }
      }
    }
  }

  mem_wb_ = std::move(next_mem_wb);
  ex_mem_ = std::move(next_ex_mem);
  id_ex_ = std::move(next_id_ex);
  if_id_ = std::move(next_if_id);
  emit_trace();
  return retired;
}

uint64_t Pipeline::run(uint64_t max_cycles) {
  const uint64_t start = stats_.cycles;
  while (!halted_ && stats_.cycles < max_cycles) {
    cycle();
  }
  return stats_.cycles - start;
}

std::optional<Retired> Pipeline::writeback() {
  const Slot& s = mem_wb_;
  if (!s.valid) {
    ++stats_.lost[static_cast<std::size_t>(s.cause)];
    return std::nullopt;
  }
  if (!s.fault.empty()) {
    throw SimError(s.fault);
  }
  Retired r;
  r.pc = s.pc;
  r.raw = s.raw;
  if (s.writes) {
    regs_[s.rd] = s.result;
    r.rd = s.rd;
    r.rd_value = s.result;
  }
  const Op op = s.in.op;
  if (is_store(op)) {
    r.store_size = static_cast<uint8_t>(access_size(op));
    r.store_addr = s.mem_addr;
    r.store_value = s.store_value;
    ++stats_.stores;
  }
  stats_.loads += is_load(op) ? 1u : 0u;
  stats_.muls += is_mul(op) ? 1u : 0u;
  stats_.divs += is_div(op) ? 1u : 0u;
  ++stats_.retired;
  if (s.exits) {
    halted_ = true;
  }
  return r;
}

Pipeline::Slot Pipeline::memory_stage() {
  Slot s = ex_mem_;
  if (!s.valid || !s.fault.empty()) {
    return s;
  }
  const Op op = s.in.op;
  const unsigned size = access_size(op);
  try {
    if (is_load(op)) {
      s.result = extend_load(op, mem_.load(s.mem_addr, size));
    } else if (is_store(op)) {
      // Store data forwarded from the instruction now in WB. This is what
      // lets a load be followed directly by a store of the loaded value,
      // and it also refreshes data that EX could not forward from a load.
      uint32_t data = s.rs2_val;
      if (config_.forwarding && s.in.rs2 != 0 && mem_wb_.valid && mem_wb_.writes &&
          mem_wb_.rd == s.in.rs2) {
        data = mem_wb_.result;
        ++stats_.bypass_store_data;
      }
      mem_.store(s.mem_addr, size, data);
      s.store_value = size == 4 ? data : data & ((1u << (8 * size)) - 1);
    }
  } catch (const SimError& e) {
    s.fault = e.what();
  }
  return s;
}

uint32_t Pipeline::forward(unsigned reg, uint32_t value) {
  // Unused register fields decode as x0, so this also skips operands the
  // instruction does not read.
  if (!config_.forwarding || reg == 0) {
    return value;
  }
  if (ex_mem_.valid && ex_mem_.writes && ex_mem_.rd == reg) {
    // A load in MEM has no data yet. Only store data can get here (the
    // hazard unit stalls every other consumer), and MEM re-forwards it.
    if (is_load(ex_mem_.in.op)) {
      return value;
    }
    ++stats_.bypass_ex_mem;
    return ex_mem_.result;
  }
  if (mem_wb_.valid && mem_wb_.writes && mem_wb_.rd == reg) {
    ++stats_.bypass_mem_wb;
    return mem_wb_.result;
  }
  return value;
}

std::optional<Pipeline::Redirect> Pipeline::execute(Slot& s) {
  if (!s.fault.empty()) {
    return std::nullopt;
  }
  const Instr& in = s.in;
  const uint32_t rs1 = forward(in.rs1, s.rs1_val);
  const uint32_t rs2 = forward(in.rs2, s.rs2_val);
  s.rs1_val = rs1;
  s.rs2_val = rs2;

  if (in.op == Op::Ecall || in.op == Op::Fence) {
    return std::nullopt;
  }
  if (is_load(in.op) || is_store(in.op)) {
    s.mem_addr = mem_address(in, rs1);
    return std::nullopt;
  }
  if (!is_branch(in.op) && !is_jump(in.op)) {
    s.result = compute(in, s.pc, rs1, rs2);
    return std::nullopt;
  }

  const bool taken = is_jump(in.op) || branch_taken(in.op, rs1, rs2);
  const uint32_t target = control_target(in, s.pc, rs1);
  const uint32_t next = taken ? target : s.pc + 4;
  const bool mispredicted = next != s.pred.next_pc;
  predictor_.resolve(in, s.pc, taken, target, mispredicted, s.pred);
  if (is_jump(in.op)) {
    s.result = s.pc + 4;
  }

  StallCause cause;
  if (is_branch(in.op)) {
    ++stats_.branches;
    stats_.branches_taken += taken ? 1u : 0u;
    stats_.branch_mispredicts += mispredicted ? 1u : 0u;
    cause = StallCause::BranchMispredict;
  } else if (in.op == Op::Jal) {
    ++stats_.jals;
    stats_.jal_misses += mispredicted ? 1u : 0u;
    cause = StallCause::JalMiss;
  } else if (ras_action(in) == RasAction::Pop) {
    ++stats_.returns;
    stats_.return_mispredicts += mispredicted ? 1u : 0u;
    cause = StallCause::ReturnMispredict;
  } else {
    ++stats_.other_jalrs;
    stats_.other_jalr_mispredicts += mispredicted ? 1u : 0u;
    cause = StallCause::JalrMispredict;
  }
  stats_.control_btb_hits += s.pred.btb_hit ? 1u : 0u;

  if (!mispredicted) {
    return std::nullopt;
  }
  return Redirect{next, cause};
}

std::optional<StallCause> Pipeline::hazard(const Slot& s) const {
  if (!s.fault.empty()) {
    return std::nullopt;  // never executes; it only has to reach WB
  }
  const Instr& in = s.in;
  if (in.op == Op::Ecall) {
    // Older instructions must have written back (the one in WB does so
    // this cycle) before the syscall reads registers and memory.
    if (id_ex_.valid || ex_mem_.valid) {
      return StallCause::Serialize;
    }
    return std::nullopt;
  }

  const bool uses_rs1 = reads_rs1(in.op) && in.rs1 != 0;
  const bool uses_rs2 = reads_rs2(in.op) && in.rs2 != 0;
  auto depends_on = [&](const Slot& producer, bool count_rs2) {
    return producer.valid && producer.writes &&
           ((uses_rs1 && producer.rd == in.rs1) ||
            (count_rs2 && uses_rs2 && producer.rd == in.rs2));
  };

  if (config_.forwarding) {
    // Load data exists only at the end of MEM, one cycle too late for EX.
    // Store data is exempt because it is forwarded into MEM instead.
    if (is_load(id_ex_.in.op) && depends_on(id_ex_, !is_store(in.op))) {
      return StallCause::LoadUse;
    }
    return std::nullopt;
  }
  // Without forwarding a value is usable only once its producer is in WB.
  if (depends_on(id_ex_, true) || depends_on(ex_mem_, true)) {
    return StallCause::DataHazard;
  }
  return std::nullopt;
}

void Pipeline::decode(Slot& s) {
  s.rs1_val = regs_[s.in.rs1];
  s.rs2_val = regs_[s.in.rs2];
  if (!s.fault.empty()) {
    return;
  }
  if (writes_rd(s.in.op)) {
    s.rd = s.in.rd;
  }
  if (s.in.op == Op::Ecall) {
    // Non-speculative here: hazard() held the ecall until every older
    // instruction had retired, so no older branch can still redirect.
    try {
      const uint32_t a0 = host_.ecall(regs_, mem_);
      if (host_.exited()) {
        s.exits = true;
        fetch_stopped_ = true;
      } else {
        s.rd = 10;
        s.result = a0;
      }
    } catch (const SimError& e) {
      s.fault = e.what();
    }
  }
  s.writes = s.rd != 0;
}

Pipeline::Slot Pipeline::fetch() {
  Slot s;
  s.valid = true;
  s.pc = pc_;
  try {
    s.raw = mem_.fetch(pc_);
  } catch (const SimError& e) {
    s.fault = e.what();
  }
  s.in = rvsim::decode(s.raw);
  if (s.fault.empty()) {
    if (s.in.op == Op::Illegal) {
      s.fault = std::format("illegal instruction {:#010x} at pc {:#010x}", s.raw, s.pc);
    } else if (s.in.op == Op::Ebreak) {
      s.fault = std::format("ebreak at pc {:#010x}", s.pc);
    }
  }
  s.pred = predictor_.predict(pc_);
  pc_ = s.pred.next_pc;
  return s;
}

}  // namespace rvsim

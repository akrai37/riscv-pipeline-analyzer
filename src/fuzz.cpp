#include "rvsim/fuzz.hpp"

#include <array>
#include <format>
#include <initializer_list>

#include "rvsim/encode.hpp"
#include "rvsim/isa.hpp"
#include "rvsim/reference.hpp"

namespace rvsim {
namespace {

using namespace enc;

// splitmix64. The standard <random> distributions differ between standard
// libraries, which would make a seed generate different programs on Linux
// and macOS.
class Rng {
 public:
  explicit Rng(uint64_t seed) : state_(seed) {}

  uint64_t next() {
    uint64_t z = (state_ += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
  }

  uint32_t below(uint32_t n) { return static_cast<uint32_t>(next() % n); }
  bool percent(unsigned p) { return below(100) < p; }

  template <typename T>
  T pick(std::initializer_list<T> items) {
    return items.begin()[below(static_cast<uint32_t>(items.size()))];
  }

 private:
  uint64_t state_;
};

// Registers with a fixed role; the operand pool never includes them.
constexpr unsigned kDataPtr = s0;               // points at the middle of the data region
constexpr unsigned kJumpTmp = t3;               // auipc/jalr target computation
constexpr unsigned kLoopCounters[] = {s1, s2};  // main body, by nesting depth
constexpr unsigned kFuncLoopCounter = s3;       // loops inside functions

// Candidates for the operand pool. ra, sp and the fixed-role registers are
// excluded; a0-a2 are included so syscalls interfere with ordinary code.
constexpr unsigned kPoolCandidates[] = {t0, t1, t2, a0, a1, a2, a3, a4, a5, t4, t5, t6};

constexpr uint32_t kDataPtrValue = FuzzProgram::kDataBase + FuzzProgram::kDataSize / 2;

// The first 256 bytes of the data region hold pointers into its middle, for
// pointer chasing. Stores never touch this table, so every entry stays a
// valid, aligned pointer.
constexpr uint32_t kPointerTableBytes = 256;
constexpr uint32_t kStoreLow = FuzzProgram::kDataBase + kPointerTableBytes;
constexpr int32_t kPointerReach = 256;  // |offset| used with a chased pointer

struct Form {
  uint32_t funct7;
  uint32_t funct3;
};

constexpr Form kAluForms[] = {{0x00, 0}, {0x20, 0}, {0x00, 1}, {0x00, 2}, {0x00, 3},
                              {0x00, 4}, {0x00, 5}, {0x20, 5}, {0x00, 6}, {0x00, 7}};
constexpr Form kMulDivForms[] = {{0x01, 0}, {0x01, 1}, {0x01, 2}, {0x01, 3},
                                 {0x01, 4}, {0x01, 5}, {0x01, 6}, {0x01, 7}};
constexpr uint32_t kImmFunct3[] = {0, 2, 3, 4, 6, 7};  // addi slti sltiu xori ori andi
constexpr uint32_t kBranchFunct3[] = {0, 1, 4, 5, 6, 7};

struct MemForm {
  uint32_t funct3;
  unsigned size;
};

constexpr MemForm kLoads[] = {{0, 1}, {1, 2}, {2, 4}, {4, 1}, {5, 2}};
constexpr MemForm kStores[] = {{0, 1}, {1, 2}, {2, 4}};

class Generator {
 public:
  Generator(uint64_t seed, const FuzzOptions& options) : rng_(seed), options_(options) {}

  FuzzProgram build(uint64_t seed) {
    choose_pool();
    prologue();

    const unsigned functions = rng_.below(options_.max_functions + 1);
    function_count_ = functions;
    const bool faults = rng_.percent(options_.fault_percent);
    const unsigned fault_at = faults ? rng_.below(options_.main_ops) : options_.main_ops;
    for (unsigned i = 0; i < options_.main_ops; ++i) {
      if (i == fault_at) {
        fault();
      }
      op(0);
    }
    emit(addi(a7, zero, 93));  // exit(a0)
    emit(ecall());

    std::vector<std::size_t> function_starts;
    for (unsigned f = 0; f < functions; ++f) {
      garbage(rng_.below(4));
      function_starts.push_back(code_.size());
      in_function_ = true;
      const unsigned len = 2 + rng_.below(10);
      for (unsigned i = 0; i < len; ++i) {
        op(0);
      }
      in_function_ = false;
      emit(jalr(zero, ra, 0));  // ret
    }
    patch_calls(function_starts);

    FuzzProgram p;
    p.seed = seed;
    p.code = std::move(code_);
    p.data.resize(FuzzProgram::kDataSize / 4);
    for (uint32_t& w : p.data) {
      w = interesting_value();
    }
    // Pointers land in [kStoreLow + reach, end - reach), word aligned, so a
    // chased access with |offset| < kPointerReach stays in bounds and above
    // the table.
    const uint32_t lo = kStoreLow + kPointerReach;
    const uint32_t hi = FuzzProgram::kDataBase + FuzzProgram::kDataSize - kPointerReach;
    for (uint32_t i = 0; i < kPointerTableBytes / 4; ++i) {
      p.data[i] = (lo + rng_.below(hi - lo)) & ~3u;
    }
    return p;
  }

 private:
  struct CallSite {
    std::size_t at;  // index of the jal, or of the auipc of an auipc/jalr pair
    unsigned function;
    bool indirect;
  };

  void emit(uint32_t word) { code_.push_back(word); }
  std::size_t here() const { return code_.size(); }
  int32_t offset(std::size_t from, std::size_t to) const {
    return 4 * (static_cast<int32_t>(to) - static_cast<int32_t>(from));
  }

  void choose_pool() {
    std::vector<unsigned> candidates(std::begin(kPoolCandidates), std::end(kPoolCandidates));
    const unsigned n = 3 + rng_.below(4);
    for (unsigned i = 0; i < n; ++i) {
      const uint32_t j = rng_.below(static_cast<uint32_t>(candidates.size()));
      pool_.push_back(candidates[j]);
      candidates.erase(candidates.begin() + j);
    }
  }

  unsigned src() {
    return rng_.percent(6) ? zero : pool_[rng_.below(static_cast<uint32_t>(pool_.size()))];
  }
  unsigned dst() {
    return rng_.percent(4) ? zero : pool_[rng_.below(static_cast<uint32_t>(pool_.size()))];
  }

  uint32_t interesting_value() {
    switch (rng_.below(8)) {
      case 0:
        return rng_.pick<uint32_t>({0, 1, 2, 0xffffffffu, 0x80000000u, 0x7fffffffu});
      case 1:
        return rng_.below(16);
      case 2:
        return static_cast<uint32_t>(-static_cast<int32_t>(rng_.below(16)));
      default:
        return static_cast<uint32_t>(rng_.next());
    }
  }

  int32_t imm12() {
    if (rng_.percent(30)) {
      return rng_.pick<int32_t>({0, 1, -1, 2047, -2048, 31, 32});
    }
    return static_cast<int32_t>(rng_.below(4096)) - 2048;
  }

  // Loads a 32-bit constant with lui + addi.
  void li(unsigned rd, uint32_t value) {
    const uint32_t hi = (value + 0x800) >> 12;
    const auto lo = static_cast<int32_t>(value - (hi << 12));
    emit(lui(rd, hi & 0xfffff));
    emit(addi(rd, rd, lo));
  }

  void prologue() {
    li(kDataPtr, kDataPtrValue);
    for (const unsigned r : pool_) {
      li(r, interesting_value());
    }
  }

  void op(unsigned depth) {
    const uint32_t roll = rng_.below(100);
    if (roll < 28) {
      alu_reg();
    } else if (roll < 46) {
      alu_imm();
    } else if (roll < 50) {
      emit(rng_.percent(50) ? lui(dst(), rng_.below(1u << 20))
                            : auipc(dst(), rng_.below(1u << 20)));
    } else if (roll < 58) {
      load();
    } else if (roll < 64) {
      pointer_chase();
    } else if (roll < 74) {
      store();
    } else if (roll < 81 && depth < 3) {
      forward_branch(depth);
    } else if (roll < 85 && depth < 3) {
      loop(depth);
    } else if (roll < 91 && !in_function_ && function_count_ > 0) {
      call();
    } else if (roll < 94) {
      indirect_skip();
    } else if (roll < 96) {
      write_syscall();
    } else if (roll < 97) {
      emit(0x0ff0000f);  // fence
    } else {
      alu_reg();
    }
  }

  void alu_reg() {
    const Form f = rng_.percent(25) ? kMulDivForms[rng_.below(8)] : kAluForms[rng_.below(10)];
    emit(r_type(f.funct7, src(), src(), f.funct3, dst(), 0x33));
  }

  void alu_imm() {
    if (rng_.percent(25)) {
      const uint32_t kind = rng_.below(3);  // slli, srli, srai
      const uint32_t funct7 = kind == 2 ? 0x20 : 0x00;
      emit(r_type(funct7, rng_.below(32), src(), kind == 0 ? 1 : 5, dst(), 0x13));
      return;
    }
    emit(i_type(imm12(), src(), kImmFunct3[rng_.below(6)], dst(), 0x13));
  }

  // An aligned offset from kDataPtr that stays inside the data region; for
  // stores, also above the pointer table.
  int32_t data_offset(unsigned size, bool for_store) {
    const uint32_t low = for_store ? kStoreLow : FuzzProgram::kDataBase;
    const uint32_t span = FuzzProgram::kDataBase + FuzzProgram::kDataSize - low;
    const auto addr = low + rng_.below(span);
    const auto off = static_cast<int32_t>(addr) - static_cast<int32_t>(kDataPtrValue);
    return off & ~static_cast<int32_t>(size - 1);
  }

  // Computes an address at run time: (src & mask) + kDataPtr lands in the
  // upper half of the data region; a negative offset reaches the lower half.
  unsigned dynamic_address(unsigned size, int32_t& off) {
    const unsigned tmp = pool_[rng_.below(static_cast<uint32_t>(pool_.size()))];
    const auto mask = static_cast<int32_t>(0x7ff & ~(size - 1));
    emit(i_type(mask, src(), 7, tmp, 0x13));  // andi tmp, src, mask
    emit(add(tmp, tmp, kDataPtr));
    // Down to kStoreLow at most, so stores also clear the pointer table.
    const uint32_t reach = kDataPtrValue - kStoreLow;
    off = -static_cast<int32_t>(rng_.below(reach / size)) * static_cast<int32_t>(size);
    return tmp;
  }

  // Loads a pointer from the table and uses it straight away as the base of
  // a load or store: a load-to-address dependency, as in list traversal.
  void pointer_chase() {
    const unsigned ptr = pool_[rng_.below(static_cast<uint32_t>(pool_.size()))];
    const auto slot = static_cast<int32_t>(4 * rng_.below(kPointerTableBytes / 4));
    emit(lw(
        ptr, kDataPtr,
        static_cast<int32_t>(FuzzProgram::kDataBase) - static_cast<int32_t>(kDataPtrValue) + slot));
    const bool is_store = rng_.percent(40);
    const MemForm m = is_store ? kStores[rng_.below(3)] : kLoads[rng_.below(5)];
    const auto off = (static_cast<int32_t>(rng_.below(2 * kPointerReach)) - kPointerReach) &
                     ~static_cast<int32_t>(m.size - 1);
    if (is_store) {
      emit(s_type(off, src(), ptr, m.funct3));
    } else {
      emit(i_type(off, ptr, m.funct3, dst(), 0x03));
    }
  }

  void load() {
    const MemForm m = kLoads[rng_.below(5)];
    int32_t off = data_offset(m.size, false);
    unsigned base = kDataPtr;
    if (rng_.percent(35)) {
      base = dynamic_address(m.size, off);
    }
    emit(i_type(off, base, m.funct3, dst(), 0x03));
  }

  void store() {
    const MemForm m = kStores[rng_.below(3)];
    int32_t off = data_offset(m.size, true);
    unsigned base = kDataPtr;
    if (rng_.percent(35)) {
      base = dynamic_address(m.size, off);
    }
    emit(s_type(off, src(), base, m.funct3));
  }

  void forward_branch(unsigned depth) {
    const std::size_t at = here();
    emit(0);
    const unsigned skipped = 1 + rng_.below(4);
    for (unsigned i = 0; i < skipped; ++i) {
      op(depth + 1);
    }
    code_[at] = b_type(offset(at, here()), src(), src(), kBranchFunct3[rng_.below(6)]);
  }

  void loop(unsigned depth) {
    unsigned counter = kFuncLoopCounter;
    if (!in_function_) {
      if (loop_depth_ >= 2) {
        alu_reg();
        return;
      }
      counter = kLoopCounters[loop_depth_];
    } else if (loop_depth_ >= 1) {
      alu_reg();
      return;
    }
    ++loop_depth_;
    emit(addi(counter, zero, static_cast<int32_t>(1 + rng_.below(4))));
    const std::size_t top = here();
    const unsigned body = 2 + rng_.below(5);
    for (unsigned i = 0; i < body; ++i) {
      op(depth + 1);
    }
    emit(addi(counter, counter, -1));
    emit(bne(counter, zero, offset(here(), top)));
    --loop_depth_;
  }

  void call() {
    const unsigned f = rng_.below(function_count_);
    const bool indirect = rng_.percent(30);
    calls_.push_back({here(), f, indirect});
    emit(0);
    if (indirect) {
      emit(0);  // auipc + jalr ra, patched once functions are placed
    }
  }

  void patch_calls(const std::vector<std::size_t>& starts) {
    for (const CallSite& c : calls_) {
      const int32_t off = offset(c.at, starts[c.function]);
      if (!c.indirect) {
        code_[c.at] = jal(ra, off);
        continue;
      }
      const auto u = static_cast<uint32_t>(off);
      const uint32_t hi = (u + 0x800) >> 12;
      code_[c.at] = auipc(kJumpTmp, hi & 0xfffff);
      code_[c.at + 1] = jalr(ra, kJumpTmp, static_cast<int32_t>(u - (hi << 12)));
    }
  }

  // auipc + jalr over 1-3 words of garbage: the first time through, the BTB
  // misses and the pipeline fetches the garbage on the wrong path.
  void indirect_skip() {
    const unsigned skipped = 1 + rng_.below(3);
    emit(auipc(kJumpTmp, 0));
    emit(jalr(zero, kJumpTmp, static_cast<int32_t>(8 + 4 * skipped)));
    garbage(skipped);
  }

  void garbage(unsigned words) {
    for (unsigned i = 0; i < words; ++i) {
      // Raw random words, or real instructions that must not take effect.
      if (rng_.percent(50)) {
        emit(static_cast<uint32_t>(rng_.next()));
      } else {
        emit(rng_.pick<uint32_t>({ebreak(), ecall(), sw(src(), kDataPtr, 0),
                                  jal(ra, -4 * static_cast<int32_t>(rng_.below(64))),
                                  r_type(0x01, src(), src(), 4, dst(), 0x33)}));
      }
    }
  }

  void write_syscall() {
    const uint32_t len = 1 + rng_.below(8);
    emit(addi(a7, zero, 64));
    emit(addi(a0, zero, 1));
    emit(addi(a1, kDataPtr, static_cast<int32_t>(rng_.below(256))));
    emit(addi(a2, zero, static_cast<int32_t>(len)));
    emit(ecall());
  }

  void fault() {
    switch (rng_.below(5)) {
      case 0:
        emit(0x00000000);  // illegal
        break;
      case 1:
        emit(ebreak());
        break;
      case 2:
        emit(lw(dst(), kDataPtr, 1));  // misaligned
        break;
      case 3:
        emit(lw(dst(), zero, 0));  // address 0 is outside memory
        break;
      default:
        emit(addi(a7, zero, 7));  // unsupported syscall
        emit(ecall());
        break;
    }
  }

  Rng rng_;
  FuzzOptions options_;
  std::vector<uint32_t> code_;
  std::vector<unsigned> pool_;
  std::vector<CallSite> calls_;
  unsigned function_count_ = 0;
  unsigned loop_depth_ = 0;
  bool in_function_ = false;
};

}  // namespace

Memory FuzzProgram::make_memory() const {
  Memory mem(kMemBase, kMemSize);
  for (std::size_t i = 0; i < code.size(); ++i) {
    mem.store(kCodeBase + static_cast<uint32_t>(4 * i), 4, code[i]);
  }
  for (std::size_t i = 0; i < data.size(); ++i) {
    mem.store(kDataBase + static_cast<uint32_t>(4 * i), 4, data[i]);
  }
  return mem;
}

std::string FuzzProgram::disassembly() const {
  std::string out = std::format("# fuzz program, seed {}\n", seed);
  for (std::size_t i = 0; i < code.size(); ++i) {
    out += std::format("{:08x}: {:08x}  {}\n", kCodeBase + 4 * i, code[i],
                       disassemble(decode(code[i])));
  }
  return out;
}

FuzzProgram generate_fuzz_program(uint64_t seed, const FuzzOptions& options) {
  return Generator(seed, options).build(seed);
}

PipelineConfig random_pipeline_config(uint64_t seed) {
  Rng rng(seed ^ 0x5bd1e9955bd1e995ull);
  PipelineConfig c;
  c.forwarding = rng.percent(75);
  c.mul_latency = 1 + rng.below(5);
  c.div_latency = 1 + rng.below(40);
  c.predictor.direction = rng.pick(
      {DirectionPredictor::NotTaken, DirectionPredictor::Btfn, DirectionPredictor::TwoBit});
  c.predictor.btb_entries = rng.pick<unsigned>({0, 1, 2, 4, 16, 64});
  c.predictor.pht_entries = rng.pick<unsigned>({1, 2, 16, 256});
  c.predictor.ras_entries = rng.pick<unsigned>({0, 1, 2, 4, 8, 16});
  return c;
}

std::string describe_config(const PipelineConfig& c) {
  return std::format("forwarding={} predictor={} btb={} pht={} ras={} mul={} div={}",
                     c.forwarding ? "on" : "off", direction_name(c.predictor.direction),
                     c.predictor.btb_entries, c.predictor.pht_entries, c.predictor.ras_entries,
                     c.mul_latency, c.div_latency);
}

FuzzRun run_fuzz_case(const FuzzProgram& program, const PipelineConfig& config,
                      uint64_t max_cycles) {
  Memory pipe_mem = program.make_memory();
  Memory ref_mem = program.make_memory();
  Pipeline pipe(pipe_mem, FuzzProgram::kCodeBase, config);
  ReferenceModel ref(ref_mem, FuzzProgram::kCodeBase);
  FuzzRun run;
  run.result = run_lockstep(pipe, ref, max_cycles);
  run.stats = pipe.stats();
  return run;
}

}  // namespace rvsim

// Directed pipeline timing tests. Each program isolates one hazard and
// checks its exact cycle cost, and is also run in lockstep against the
// reference model so every test checks correctness as well as timing.

#include "rvsim/pipeline.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "encode.hpp"
#include "rvsim/error.hpp"
#include "rvsim/lockstep.hpp"
#include "rvsim/memory.hpp"
#include "rvsim/reference.hpp"

namespace {

using namespace enc;
using rvsim::Memory;
using rvsim::Pipeline;
using rvsim::PipelineConfig;
using rvsim::PipelineStats;
using rvsim::StallCause;

constexpr uint32_t kBase = 0x1000;
constexpr uint32_t kData = 0x2000;
constexpr uint32_t kMemSize = 0x4000;

struct Result {
  PipelineStats stats;
  rvsim::RegFile regs;
  Memory mem;

  uint64_t lost(StallCause c) const { return stats.lost_cycles(c); }
};

using Init = std::function<void(Memory&)>;

Memory load(const std::vector<uint32_t>& program, const Init& init) {
  Memory mem(kBase, kMemSize);
  for (std::size_t i = 0; i < program.size(); ++i) {
    mem.store(kBase + static_cast<uint32_t>(4 * i), 4, program[i]);
  }
  if (init) {
    init(mem);
  }
  return mem;
}

// Runs `program` on the pipeline in lockstep with the reference model and
// checks the exit and the cycle accounting identity.
Result run(const std::vector<uint32_t>& program, const PipelineConfig& config = {},
           const Init& init = {}) {
  Memory mem = load(program, init);
  Memory ref_mem = load(program, init);
  Pipeline pipe(mem, kBase, config);
  rvsim::ReferenceModel ref(ref_mem, kBase);
  const rvsim::LockstepResult r = rvsim::run_lockstep(pipe, ref, 100'000);
  EXPECT_EQ(r.outcome, rvsim::LockstepResult::Outcome::Exited) << r.message;
  const PipelineStats& s = pipe.stats();
  EXPECT_EQ(s.cycles, s.retired + s.total_lost());
  return {s, pipe.regs(), mem};
}

// Appends the exit sequence. `addi a7` directly before `ecall` always costs
// two serialize cycles: the ecall waits in ID until the addi has written back.
std::vector<uint32_t> with_exit(std::vector<uint32_t> body) {
  body.push_back(addi(a7, zero, 93));
  body.push_back(ecall());
  return body;
}

PipelineConfig no_forwarding() {
  PipelineConfig c;
  c.forwarding = false;
  return c;
}

void put_word(Memory& mem, uint32_t addr, uint32_t value) {
  mem.store(addr, 4, value);
}

TEST(PipelineTiming, MinimalProgram) {
  const Result r = run(with_exit({}));
  EXPECT_EQ(r.stats.retired, 2u);
  EXPECT_EQ(r.stats.cycles, 8u);  // 4 fill + 2 instructions + 2 serialize
  EXPECT_EQ(r.lost(StallCause::Fill), 4u);
  EXPECT_EQ(r.lost(StallCause::Serialize), 2u);
}

TEST(PipelineTiming, IndependentInstructionsIssueEveryCycle) {
  std::vector<uint32_t> body;
  for (unsigned i = 0; i < 10; ++i) {
    body.push_back(addi(t0 + (i % 3), zero, static_cast<int32_t>(i)));
  }
  const Result r = run(with_exit(body));
  EXPECT_EQ(r.stats.cycles, 12u + 4u + 2u);
  EXPECT_EQ(r.stats.total_lost(), 6u);
}

TEST(PipelineTiming, AluToAluForwardingHasNoStall) {
  const Result r = run(with_exit({addi(t0, zero, 1), add(t1, t0, t0), add(t2, t1, t0)}));
  EXPECT_EQ(r.lost(StallCause::DataHazard), 0u);
  EXPECT_EQ(r.lost(StallCause::LoadUse), 0u);
  EXPECT_EQ(r.regs[t2], 3u);
}

TEST(PipelineTiming, WithoutForwardingStallsDependOnDistance) {
  // Distance 1: the consumer waits two cycles for writeback.
  EXPECT_EQ(run(with_exit({addi(t0, zero, 1), add(t1, t0, t0)}), no_forwarding())
                .lost(StallCause::DataHazard),
            2u);
  // Distance 2: one cycle.
  EXPECT_EQ(run(with_exit({addi(t0, zero, 1), nop(), add(t1, t0, t0)}), no_forwarding())
                .lost(StallCause::DataHazard),
            1u);
  // Distance 3: WB writes in the first half of the cycle ID reads in.
  const Result far =
      run(with_exit({addi(t0, zero, 1), nop(), nop(), add(t1, t0, t0)}), no_forwarding());
  EXPECT_EQ(far.lost(StallCause::DataHazard), 0u);
  EXPECT_EQ(far.regs[t1], 2u);
}

TEST(PipelineTiming, LoadUseCostsOneCycle) {
  const auto init = [](Memory& m) { put_word(m, kData, 21); };
  const Result r = run(with_exit({lui(s0, kData >> 12), lw(t0, s0, 0), add(t1, t0, t0)}), {}, init);
  EXPECT_EQ(r.lost(StallCause::LoadUse), 1u);
  EXPECT_EQ(r.regs[t1], 42u);
  // Without forwarding: lui -> lw and lw -> add each wait two cycles.
  EXPECT_EQ(
      run(with_exit({lui(s0, kData >> 12), lw(t0, s0, 0), add(t1, t0, t0)}), no_forwarding(), init)
          .lost(StallCause::DataHazard),
      4u);
}

TEST(PipelineTiming, LoadIntoBranchCostsOneCycle) {
  const auto init = [](Memory& m) { put_word(m, kData, 0); };
  const Result r =
      run(with_exit({lui(s0, kData >> 12), lw(t0, s0, 0), bne(t0, zero, 8), addi(a0, zero, 1)}), {},
          init);
  EXPECT_EQ(r.lost(StallCause::LoadUse), 1u);
  EXPECT_EQ(r.regs[a0], 1u);
}

TEST(PipelineTiming, LoadThenStoreOfLoadedValueDoesNotStall) {
  // The store's data is forwarded MEM/WB -> MEM, so no load-use stall.
  const auto init = [](Memory& m) { put_word(m, kData, 0xcafef00du); };
  const Result r = run(with_exit({lui(s0, kData >> 12), lw(t0, s0, 0), sw(t0, s0, 4)}), {}, init);
  EXPECT_EQ(r.lost(StallCause::LoadUse), 0u);
  EXPECT_EQ(r.mem.load(kData + 4, 4), 0xcafef00du);
}

TEST(PipelineTiming, LoadThenStoreThroughLoadedAddressStalls) {
  // The loaded value is the store's address, which EX needs.
  const auto init = [](Memory& m) {
    put_word(m, kData, kData + 8);
    put_word(m, kData + 8, 0xffffffffu);
  };
  const Result r = run(with_exit({lui(s0, kData >> 12), lw(t0, s0, 0), sw(zero, t0, 0)}), {}, init);
  EXPECT_EQ(r.lost(StallCause::LoadUse), 1u);
  EXPECT_EQ(r.mem.load(kData + 8, 4), 0u);
}

TEST(PipelineTiming, StoreDataForwardedFromAlu) {
  const Result r =
      run(with_exit({lui(s0, kData >> 12), addi(t0, zero, 42), sw(t0, s0, 0), lw(t1, s0, 0)}));
  EXPECT_EQ(r.mem.load(kData, 4), 42u);
  EXPECT_EQ(r.regs[t1], 42u);
  EXPECT_EQ(r.stats.total_lost(), 6u);  // fill + serialize only
}

TEST(PipelineTiming, ZeroRegisterIsNeverForwardedOrStalledOn) {
  // Without forwarding, a "dependency" through x0 must not stall.
  const Result alu = run(with_exit({addi(zero, zero, 5), add(a0, zero, zero)}), no_forwarding());
  EXPECT_EQ(alu.lost(StallCause::DataHazard), 0u);
  EXPECT_EQ(alu.regs[a0], 0u);
  // A load into x0 is not a load-use hazard, and its value is discarded.
  const auto init = [](Memory& m) { put_word(m, kData, 77); };
  const Result ld =
      run(with_exit({lui(s0, kData >> 12), lw(zero, s0, 0), add(a0, zero, zero)}), {}, init);
  EXPECT_EQ(ld.lost(StallCause::LoadUse), 0u);
  EXPECT_EQ(ld.regs[a0], 0u);
}

TEST(PipelineTiming, MultiplyAndDivideHoldExecute) {
  const Result m = run(with_exit({addi(t0, zero, 3), mul(t1, t0, t0), add(t2, t1, t1)}));
  EXPECT_EQ(m.lost(StallCause::MulDiv), 2u);  // 3-cycle multiply
  EXPECT_EQ(m.regs[t2], 18u);
  const Result d = run(with_exit({addi(t0, zero, 9), div(t1, t0, t0)}));
  EXPECT_EQ(d.lost(StallCause::MulDiv), 31u);  // 32-cycle divide
  PipelineConfig fast;
  fast.mul_latency = 1;
  fast.div_latency = 1;
  EXPECT_EQ(run(with_exit({addi(t0, zero, 3), mul(t1, t0, t0), div(t2, t1, t0)}), fast)
                .lost(StallCause::MulDiv),
            0u);
}

TEST(PipelineTiming, MultiplyOperandsAreCapturedOnTheFirstCycle) {
  // While the multiply holds EX, the instruction that produced its operand
  // retires; the result must still use the value forwarded on cycle one.
  const Result r = run(with_exit({addi(t0, zero, 5), mul(t1, t0, t0), addi(t0, zero, 1)}));
  EXPECT_EQ(r.regs[t1], 25u);
}

// for (t0 = 5; t0 != 0; --t0) {}
std::vector<uint32_t> countdown_loop() {
  return with_exit({addi(t0, zero, 5), addi(t0, t0, -1), bne(t0, zero, -4)});
}

TEST(PipelineBranches, TwoBitLoopMispredictsOnEntryAndExit) {
  // 1st bne: BTB miss while taken. 2nd-4th: predicted taken. 5th: predicted
  // taken, falls through. Two mispredictions, two cycles each.
  const Result r = run(countdown_loop());
  EXPECT_EQ(r.stats.branches, 5u);
  EXPECT_EQ(r.stats.branch_mispredicts, 2u);
  EXPECT_EQ(r.lost(StallCause::BranchMispredict), 4u);
}

TEST(PipelineBranches, NotTakenMispredictsEveryTakenBranch) {
  PipelineConfig c;
  c.predictor.direction = rvsim::DirectionPredictor::NotTaken;
  const Result r = run(countdown_loop(), c);
  EXPECT_EQ(r.stats.branch_mispredicts, 4u);
  EXPECT_EQ(r.lost(StallCause::BranchMispredict), 8u);
}

TEST(PipelineBranches, BtfnPredictsBackwardBranchesTaken) {
  PipelineConfig c;
  c.predictor.direction = rvsim::DirectionPredictor::Btfn;
  EXPECT_EQ(run(countdown_loop(), c).stats.branch_mispredicts, 2u);
}

TEST(PipelineBranches, WithoutBtbEveryTakenBranchMispredicts) {
  PipelineConfig c;
  c.predictor.btb_entries = 0;
  EXPECT_EQ(run(countdown_loop(), c).stats.branch_mispredicts, 4u);
}

TEST(PipelineBranches, JalCostsTwoCyclesOnlyOnBtbMiss) {
  // The jal runs twice; the ebreak it skips is fetched on the wrong path the
  // first time and must be squashed without faulting.
  const Result r = run(with_exit({
      addi(t1, zero, 2),
      jal(zero, 8),  // L:
      ebreak(),
      addi(t1, t1, -1),
      bne(t1, zero, -12),
  }));
  EXPECT_EQ(r.stats.jals, 2u);
  EXPECT_EQ(r.stats.jal_misses, 1u);
  EXPECT_EQ(r.lost(StallCause::JalMiss), 2u);
}

// Calls f from two call sites, four times each; f just returns.
std::vector<uint32_t> two_call_sites() {
  return {
      addi(s1, zero, 4),   // 0x00
      jal(ra, 0x18),       // 0x04 loop: call f (site A)
      jal(ra, 0x14),       // 0x08       call f (site B)
      addi(s1, s1, -1),    // 0x0c
      bne(s1, zero, -12),  // 0x10
      addi(a7, zero, 93),  // 0x14
      ecall(),             // 0x18
      jalr(zero, ra, 0),   // 0x1c f: ret
  };
}

TEST(PipelineBranches, ReturnAddressStackPredictsReturns) {
  const Result r = run(two_call_sites());
  EXPECT_EQ(r.stats.returns, 8u);
  EXPECT_EQ(r.stats.return_mispredicts, 1u);  // first return: not yet in the BTB
}

TEST(PipelineBranches, WithoutRasReturnsUseTheLastTarget) {
  // The BTB remembers the previous return address, which alternates between
  // the two call sites, so every return mispredicts.
  PipelineConfig c;
  c.predictor.ras_entries = 0;
  const Result r = run(two_call_sites(), c);
  EXPECT_EQ(r.stats.return_mispredicts, 8u);
  EXPECT_EQ(r.lost(StallCause::ReturnMispredict), 16u);
}

TEST(PipelineFaults, WrongPathFetchOutsideMemoryIsSquashed) {
  // The last word of memory is a jal back to kBase + 4; the fall-through
  // fetch after it is outside memory, but that instruction never reaches WB.
  constexpr uint32_t kLast = kBase + kMemSize - 4;
  const auto init = [](Memory& m) {
    put_word(m, kLast, jal(zero, static_cast<int32_t>(kBase + 4) - static_cast<int32_t>(kLast)));
  };
  const Result r =
      run({jal(zero, static_cast<int32_t>(kLast - kBase)), addi(a7, zero, 93), ecall()}, {}, init);
  EXPECT_EQ(r.stats.retired, 4u);
}

TEST(PipelineFaults, IllegalInstructionFaultsAtWriteback) {
  Memory mem = load({addi(t0, zero, 1), 0x00000000, addi(t1, zero, 2)}, {});
  Pipeline pipe(mem, kBase, {});
  EXPECT_THROW(pipe.run(1000), rvsim::SimError);
  EXPECT_EQ(pipe.stats().retired, 1u);  // only the instruction before it
  EXPECT_EQ(pipe.regs()[t1], 0u);       // nothing after it retired
  // The faulting cycle ends the run and is not counted.
  EXPECT_EQ(pipe.stats().cycles, pipe.stats().retired + pipe.stats().total_lost());
}

TEST(PipelineFaults, StoreAfterFaultingLoadDoesNotWrite) {
  const auto init = [](Memory& m) { put_word(m, kData, 5); };
  Memory mem = load({lui(s0, kData >> 12), lw(t0, zero, 0), sw(zero, s0, 0)}, init);
  Pipeline pipe(mem, kBase, {});
  EXPECT_THROW(pipe.run(1000), rvsim::SimError);
  EXPECT_EQ(mem.load(kData, 4), 5u);
}

TEST(PipelineSyscalls, EcallResultIsForwarded) {
  const auto init = [](Memory& m) { m.store(kData, 4, 0x0a6968u); };  // "hi\n"
  const Result r = run(with_exit({lui(a1, kData >> 12), addi(a0, zero, 1), addi(a2, zero, 3),
                                  addi(a7, zero, 64), ecall(), add(a3, a0, a0)}),
                       {}, init);
  EXPECT_EQ(r.regs[a3], 6u);
}

TEST(PipelineTrace, OneLinePerCycle) {
  std::FILE* f = std::tmpfile();
  ASSERT_NE(f, nullptr);
  Memory mem = load(with_exit({addi(t0, zero, 1)}), {});
  Pipeline pipe(mem, kBase, {});
  pipe.set_trace(f);
  pipe.run(100);
  std::rewind(f);
  std::string text;
  for (int c = std::fgetc(f); c != EOF; c = std::fgetc(f)) {
    text.push_back(static_cast<char>(c));
  }
  std::fclose(f);
  EXPECT_EQ(static_cast<uint64_t>(std::count(text.begin(), text.end(), '\n')), pipe.stats().cycles);
  EXPECT_NE(text.find("stall: serialize"), std::string::npos);
  EXPECT_NE(text.find("addi t0, zero, 1"), std::string::npos);
}

}  // namespace

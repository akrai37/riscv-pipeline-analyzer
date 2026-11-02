#include "rvsim/fuzz.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include "rvsim/pipeline.hpp"

namespace {

using rvsim::FuzzProgram;
using rvsim::LockstepResult;
using rvsim::PipelineStats;
using rvsim::StallCause;

constexpr uint64_t kSeeds = 300;

TEST(Fuzz, GenerationIsDeterministic) {
  const FuzzProgram a = rvsim::generate_fuzz_program(42);
  const FuzzProgram b = rvsim::generate_fuzz_program(42);
  EXPECT_EQ(a.code, b.code);
  EXPECT_EQ(a.data, b.data);
  EXPECT_NE(a.code, rvsim::generate_fuzz_program(43).code);
}

// Every seed under the default configuration and under a seed-derived
// random one. Also checks that the generated programs exercise what they
// are meant to: each hazard type, each bypass path, and fault matching.
TEST(Fuzz, PipelineMatchesReferenceAndCoversHazards) {
  uint64_t exited = 0;
  uint64_t faulted = 0;
  PipelineStats total;
  for (uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const FuzzProgram program = rvsim::generate_fuzz_program(seed);
    for (const rvsim::PipelineConfig& config :
         {rvsim::PipelineConfig{}, rvsim::random_pipeline_config(seed)}) {
      const rvsim::FuzzRun run = rvsim::run_fuzz_case(program, config);
      ASSERT_TRUE(run.result.ok())
          << "seed " << seed << " [" << rvsim::describe_config(config)
          << "]: " << rvsim::outcome_name(run.result.outcome) << "\n"
          << run.result.message << "\nreproduce: rvsim-fuzz --dump " << seed;
      EXPECT_EQ(run.stats.cycles, run.stats.retired + run.stats.total_lost());
      exited += run.result.outcome == LockstepResult::Outcome::Exited ? 1 : 0;
      faulted += run.result.outcome == LockstepResult::Outcome::Faulted ? 1 : 0;
      for (std::size_t i = 0; i < rvsim::kStallCauses; ++i) {
        total.lost[i] += run.stats.lost[i];
      }
      total.bypass_ex_mem += run.stats.bypass_ex_mem;
      total.bypass_mem_wb += run.stats.bypass_mem_wb;
      total.bypass_store_data += run.stats.bypass_store_data;
    }
  }
  EXPECT_GT(exited, kSeeds);
  EXPECT_GT(faulted, 0u);
  for (std::size_t i = 0; i < rvsim::kStallCauses; ++i) {
    EXPECT_GT(total.lost[i], 0u) << "no fuzz program produced "
                                 << rvsim::stall_name(static_cast<StallCause>(i));
  }
  EXPECT_GT(total.bypass_ex_mem, 0u);
  EXPECT_GT(total.bypass_mem_wb, 0u);
  EXPECT_GT(total.bypass_store_data, 0u);
}

}  // namespace

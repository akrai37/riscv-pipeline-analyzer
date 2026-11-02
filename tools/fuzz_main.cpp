// rvsim-fuzz: differential fuzzing of the pipeline against the reference
// model. Every seed is run under the default configuration and under a
// configuration drawn from the seed; any divergence is reported with the
// seed needed to reproduce it.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "rvsim/fuzz.hpp"
#include "rvsim/pipeline.hpp"

namespace {

void usage(std::FILE* out) {
  std::fprintf(out,
               "usage: rvsim-fuzz [--seeds N] [--start S] [--dump SEED]\n"
               "\n"
               "  --seeds N    number of seeds to run (default: 1000)\n"
               "  --start S    first seed (default: 1)\n"
               "  --dump SEED  print the program generated for SEED and exit\n");
}

uint64_t parse(const char* flag, const char* v) {
  char* end = nullptr;
  const unsigned long long n = std::strtoull(v, &end, 10);
  if (*v == '\0' || *end != '\0') {
    std::fprintf(stderr, "rvsim-fuzz: %s needs a non-negative integer\n", flag);
    std::exit(2);
  }
  return n;
}

struct Totals {
  uint64_t runs = 0;
  uint64_t exited = 0;
  uint64_t faulted = 0;
  uint64_t diverged = 0;
  uint64_t limit = 0;
  rvsim::PipelineStats stats;

  void add(const rvsim::FuzzRun& run) {
    ++runs;
    switch (run.result.outcome) {
      case rvsim::LockstepResult::Outcome::Exited:
        ++exited;
        break;
      case rvsim::LockstepResult::Outcome::Faulted:
        ++faulted;
        break;
      case rvsim::LockstepResult::Outcome::Limit:
        ++limit;
        break;
      case rvsim::LockstepResult::Outcome::Diverged:
        ++diverged;
        break;
    }
    const rvsim::PipelineStats& s = run.stats;
    stats.cycles += s.cycles;
    stats.retired += s.retired;
    for (std::size_t i = 0; i < rvsim::kStallCauses; ++i) {
      stats.lost[i] += s.lost[i];
    }
    stats.branch_mispredicts += s.branch_mispredicts;
    stats.returns += s.returns;
    stats.return_mispredicts += s.return_mispredicts;
    stats.bypass_ex_mem += s.bypass_ex_mem;
    stats.bypass_mem_wb += s.bypass_mem_wb;
    stats.bypass_store_data += s.bypass_store_data;
  }
};

}  // namespace

int main(int argc, char** argv) {
  uint64_t seeds = 1000;
  uint64_t start = 1;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      usage(stdout);
      return 0;
    }
    if (i + 1 >= argc) {
      usage(stderr);
      return 2;
    }
    if (arg == "--seeds") {
      seeds = parse("--seeds", argv[++i]);
    } else if (arg == "--start") {
      start = parse("--start", argv[++i]);
    } else if (arg == "--dump") {
      std::fputs(rvsim::generate_fuzz_program(parse("--dump", argv[++i])).disassembly().c_str(),
                 stdout);
      return 0;
    } else {
      usage(stderr);
      return 2;
    }
  }

  Totals totals;
  for (uint64_t seed = start; seed < start + seeds; ++seed) {
    const rvsim::FuzzProgram program = rvsim::generate_fuzz_program(seed);
    for (const rvsim::PipelineConfig& config :
         {rvsim::PipelineConfig{}, rvsim::random_pipeline_config(seed)}) {
      const rvsim::FuzzRun run = rvsim::run_fuzz_case(program, config);
      totals.add(run);
      if (!run.result.ok()) {
        std::printf("seed %llu [%s]: %s\n  %s\n  reproduce: rvsim-fuzz --dump %llu\n",
                    static_cast<unsigned long long>(seed), rvsim::describe_config(config).c_str(),
                    rvsim::outcome_name(run.result.outcome), run.result.message.c_str(),
                    static_cast<unsigned long long>(seed));
      }
    }
  }

  const rvsim::PipelineStats& s = totals.stats;
  std::printf(
      "%llu runs (%llu seeds x 2 configs): %llu exited, %llu faulted as expected, "
      "%llu hit the cycle limit, %llu diverged\n",
      static_cast<unsigned long long>(totals.runs), static_cast<unsigned long long>(seeds),
      static_cast<unsigned long long>(totals.exited),
      static_cast<unsigned long long>(totals.faulted),
      static_cast<unsigned long long>(totals.limit),
      static_cast<unsigned long long>(totals.diverged));
  std::printf("%llu instructions retired in %llu cycles\n",
              static_cast<unsigned long long>(s.retired),
              static_cast<unsigned long long>(s.cycles));
  std::printf("lost cycles:");
  for (std::size_t i = 0; i < rvsim::kStallCauses; ++i) {
    std::printf(" %s=%llu", rvsim::stall_name(static_cast<rvsim::StallCause>(i)),
                static_cast<unsigned long long>(s.lost[i]));
  }
  std::printf("\nbypasses: ex/mem->ex=%llu mem/wb->ex=%llu mem/wb->mem(store data)=%llu\n",
              static_cast<unsigned long long>(s.bypass_ex_mem),
              static_cast<unsigned long long>(s.bypass_mem_wb),
              static_cast<unsigned long long>(s.bypass_store_data));
  return totals.diverged == 0 && totals.limit == 0 ? 0 : 1;
}

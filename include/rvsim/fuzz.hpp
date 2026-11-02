#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rvsim/lockstep.hpp"
#include "rvsim/memory.hpp"
#include "rvsim/pipeline.hpp"

// Constrained random program generation for differential testing of the
// pipeline against the reference model.
//
// Generated programs are built to be useful to the pipeline rather than
// merely random:
//  - operands come from a pool of 3-6 registers (plus occasional x0), so
//    back-to-back dependencies, load-use pairs and forwarding are common;
//  - loads and stores stay inside a 4 KiB data region, through a fixed
//    base register or through an address computed and masked at run time;
//  - control flow always terminates: forward branches, loops of 1-4
//    iterations nested at most two deep, and calls to leaf functions;
//  - indirect jumps skip over garbage words, which the pipeline fetches on
//    the wrong path and must squash;
//  - some programs end in a fault, which both models must raise on the
//    same instruction.
namespace rvsim {

struct FuzzProgram {
  static constexpr uint32_t kMemBase = 0x10000;
  static constexpr uint32_t kMemSize = 0x10000;
  static constexpr uint32_t kCodeBase = 0x10000;
  static constexpr uint32_t kDataBase = 0x18000;
  static constexpr uint32_t kDataSize = 0x1000;

  uint64_t seed = 0;
  std::vector<uint32_t> code;
  std::vector<uint32_t> data;

  Memory make_memory() const;
  std::string disassembly() const;
};

struct FuzzOptions {
  unsigned main_ops = 120;  // top-level operations in the main body
  unsigned max_functions = 3;
  unsigned fault_percent = 5;  // chance of ending in a fault instead of exit
};

FuzzProgram generate_fuzz_program(uint64_t seed, const FuzzOptions& options = {});

// A pipeline configuration drawn from `seed`: forwarding on or off, any
// predictor, small or absent BTB/PHT/RAS, and varied mul/div latencies.
PipelineConfig random_pipeline_config(uint64_t seed);

std::string describe_config(const PipelineConfig& config);

struct FuzzRun {
  LockstepResult result;
  PipelineStats stats;
};

FuzzRun run_fuzz_case(const FuzzProgram& program, const PipelineConfig& config,
                      uint64_t max_cycles = 2'000'000);

}  // namespace rvsim

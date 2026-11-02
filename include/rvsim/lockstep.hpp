#pragma once

#include <cstdint>
#include <string>

#include "rvsim/pipeline.hpp"
#include "rvsim/reference.hpp"

namespace rvsim {

struct LockstepResult {
  enum class Outcome {
    Exited,    // both models exited with identical state and output
    Faulted,   // both models faulted on the same instruction
    Limit,     // cycle limit reached with no divergence so far
    Diverged,  // the models disagree; `message` has the first difference
  };

  Outcome outcome = Outcome::Limit;
  uint64_t compared = 0;  // retired instructions checked
  std::string message;

  bool ok() const { return outcome == Outcome::Exited || outcome == Outcome::Faulted; }
};

const char* outcome_name(LockstepResult::Outcome outcome);

// Runs the pipeline and steps the reference model once per instruction the
// pipeline retires, comparing the two Retired records (PC, encoding,
// register write, store). At exit it also compares the exit code, the
// program's output and the full register file. The two models must use
// separate Memory instances loaded with the same program.
LockstepResult run_lockstep(Pipeline& pipe, ReferenceModel& ref, uint64_t max_cycles);

std::string format_retired(const Retired& r);

}  // namespace rvsim

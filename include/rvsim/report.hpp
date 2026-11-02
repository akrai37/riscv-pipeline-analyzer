#pragma once

#include <string>

#include "rvsim/pipeline.hpp"

namespace rvsim {

// One run as a JSON object: the configuration, every counter in
// PipelineStats (lost cycles keyed by stall_name()), and the derived CPI.
// Consumed by scripts/run_experiments.py.
std::string stats_json(const PipelineConfig& config, const PipelineStats& stats);

}  // namespace rvsim

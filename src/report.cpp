#include "rvsim/report.hpp"

#include <format>

namespace rvsim {

std::string stats_json(const PipelineConfig& c, const PipelineStats& s) {
  std::string lost;
  for (std::size_t i = 0; i < kStallCauses; ++i) {
    lost += std::format("{}\"{}\": {}", i == 0 ? "" : ", ", stall_name(static_cast<StallCause>(i)),
                        s.lost[i]);
  }
  return std::format(
      "{{\n"
      "  \"config\": {{\"forwarding\": {}, \"predictor\": \"{}\", \"btb_entries\": {}, "
      "\"pht_entries\": {}, \"ras_entries\": {}, \"mul_latency\": {}, \"div_latency\": {}}},\n"
      "  \"cycles\": {},\n"
      "  \"instructions\": {},\n"
      "  \"cpi\": {:.6f},\n"
      "  \"lost\": {{{}}},\n"
      "  \"loads\": {}, \"stores\": {}, \"muls\": {}, \"divs\": {},\n"
      "  \"branches\": {}, \"branches_taken\": {}, \"branch_mispredicts\": {},\n"
      "  \"jals\": {}, \"jal_misses\": {},\n"
      "  \"returns\": {}, \"return_mispredicts\": {},\n"
      "  \"other_jalrs\": {}, \"other_jalr_mispredicts\": {},\n"
      "  \"control_btb_hits\": {},\n"
      "  \"bypass_ex_mem\": {}, \"bypass_mem_wb\": {}, \"bypass_store_data\": {}\n"
      "}}\n",
      c.forwarding ? "true" : "false", direction_name(c.predictor.direction),
      c.predictor.btb_entries, c.predictor.pht_entries, c.predictor.ras_entries, c.mul_latency,
      c.div_latency, s.cycles, s.retired, s.cpi(), lost, s.loads, s.stores, s.muls, s.divs,
      s.branches, s.branches_taken, s.branch_mispredicts, s.jals, s.jal_misses, s.returns,
      s.return_mispredicts, s.other_jalrs, s.other_jalr_mispredicts, s.control_btb_hits,
      s.bypass_ex_mem, s.bypass_mem_wb, s.bypass_store_data);
}

}  // namespace rvsim

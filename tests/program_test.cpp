// Runs every prebuilt program (the workloads and the riscv-tests suites) on
// the reference model and, in lockstep with it, on the pipeline under
// several configurations. Each program checks its own results and must
// exit(0).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

#include "rvsim/elf.hpp"
#include "rvsim/lockstep.hpp"
#include "rvsim/memory.hpp"
#include "rvsim/pipeline.hpp"
#include "rvsim/platform.hpp"
#include "rvsim/reference.hpp"

namespace {

namespace fs = std::filesystem;

constexpr uint64_t kInstructionLimit = 50'000'000;

std::vector<std::string> elf_files(const char* dir) {
  std::vector<std::string> files;
  if (fs::is_directory(dir)) {
    for (const auto& entry : fs::directory_iterator(dir)) {
      if (entry.path().extension() == ".elf") {
        files.push_back(entry.path().string());
      }
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::string test_name(const ::testing::TestParamInfo<std::string>& info) {
  std::string name = fs::path(info.param).stem().string();
  std::replace(name.begin(), name.end(), '-', '_');
  return name;
}

struct Outcome {
  bool halted;
  int exit_code;
  std::string output;
};

Outcome run(const std::string& path) {
  rvsim::Memory mem(rvsim::kRamBase, rvsim::kRamSize);
  const uint32_t entry = rvsim::load_elf(rvsim::read_file(path), mem);
  rvsim::ReferenceModel ref(mem, entry);
  ref.run(kInstructionLimit);
  return {ref.halted(), ref.exit_code(), ref.host().output()};
}

class Workload : public ::testing::TestWithParam<std::string> {};
TEST_P(Workload, ExitsZeroOnReferenceModel) {
  // A workload's exit code is the number of failed self-checks.
  const Outcome out = run(GetParam());
  ASSERT_TRUE(out.halted) << "no exit within " << kInstructionLimit << " instructions";
  EXPECT_EQ(out.exit_code, 0) << out.output;
}
INSTANTIATE_TEST_SUITE_P(Prebuilt, Workload, ::testing::ValuesIn(elf_files(RVSIM_WORKLOAD_DIR)),
                         test_name);

class RiscvTests : public ::testing::TestWithParam<std::string> {};
TEST_P(RiscvTests, PassesOnReferenceModel) {
  // See tests/riscv-tests/env/riscv_test.h for the exit-code convention.
  const Outcome out = run(GetParam());
  ASSERT_TRUE(out.halted) << "no exit within " << kInstructionLimit << " instructions";
  EXPECT_EQ(out.exit_code, 0) << "failed test case " << (out.exit_code >> 1);
}
INSTANTIATE_TEST_SUITE_P(Prebuilt, RiscvTests,
                         ::testing::ValuesIn(elf_files(RVSIM_RISCV_TESTS_DIR)), test_name);

// Pipeline configurations every program is checked under, chosen to cover
// each mechanism both on and off.
struct NamedConfig {
  const char* name;
  rvsim::PipelineConfig config;
};

std::vector<NamedConfig> lockstep_configs() {
  std::vector<NamedConfig> out;
  out.push_back({"default", {}});

  rvsim::PipelineConfig bare;
  bare.forwarding = false;
  bare.predictor.direction = rvsim::DirectionPredictor::NotTaken;
  bare.predictor.ras_entries = 0;
  out.push_back({"no_forwarding_not_taken_no_ras", bare});

  rvsim::PipelineConfig btfn;
  btfn.predictor.direction = rvsim::DirectionPredictor::Btfn;
  btfn.predictor.btb_entries = 0;
  out.push_back({"btfn_no_btb", btfn});

  rvsim::PipelineConfig tiny;
  tiny.predictor.btb_entries = 1;
  tiny.predictor.pht_entries = 1;
  tiny.predictor.ras_entries = 1;
  tiny.mul_latency = 1;
  tiny.div_latency = 1;
  out.push_back({"one_entry_tables_single_cycle_muldiv", tiny});
  return out;
}

std::vector<std::string> all_programs() {
  std::vector<std::string> files = elf_files(RVSIM_WORKLOAD_DIR);
  const std::vector<std::string> tests = elf_files(RVSIM_RISCV_TESTS_DIR);
  files.insert(files.end(), tests.begin(), tests.end());
  return files;
}

using LockstepParam = std::tuple<std::string, std::size_t>;

class PipelineLockstep : public ::testing::TestWithParam<LockstepParam> {};

TEST_P(PipelineLockstep, MatchesReferenceModel) {
  const auto& [path, config_index] = GetParam();
  const rvsim::PipelineConfig config = lockstep_configs()[config_index].config;
  const std::vector<uint8_t> image = rvsim::read_file(path);

  rvsim::Memory mem(rvsim::kRamBase, rvsim::kRamSize);
  rvsim::Pipeline pipe(mem, rvsim::load_elf(image, mem), config);
  rvsim::Memory ref_mem(rvsim::kRamBase, rvsim::kRamSize);
  rvsim::ReferenceModel ref(ref_mem, rvsim::load_elf(image, ref_mem));

  const rvsim::LockstepResult r = rvsim::run_lockstep(pipe, ref, 10 * kInstructionLimit);
  ASSERT_EQ(r.outcome, rvsim::LockstepResult::Outcome::Exited) << r.message;
  EXPECT_EQ(pipe.exit_code(), 0) << pipe.host().output();
  const rvsim::PipelineStats& s = pipe.stats();
  EXPECT_EQ(s.cycles, s.retired + s.total_lost());
}

INSTANTIATE_TEST_SUITE_P(
    Prebuilt, PipelineLockstep,
    ::testing::Combine(::testing::ValuesIn(all_programs()),
                       ::testing::Range<std::size_t>(0, lockstep_configs().size())),
    [](const ::testing::TestParamInfo<LockstepParam>& info) {
      std::string name = fs::path(std::get<0>(info.param)).stem().string();
      std::replace(name.begin(), name.end(), '-', '_');
      return name + "__" + lockstep_configs()[std::get<1>(info.param)].name;
    });

// Guards against a missing or partially built program directory silently
// shrinking the test set.
TEST(PrebuiltPrograms, AllPresent) {
  EXPECT_EQ(elf_files(RVSIM_WORKLOAD_DIR).size(), 7u);
  EXPECT_EQ(elf_files(RVSIM_RISCV_TESTS_DIR).size(), 48u);  // 40 rv32ui + 8 rv32um
}

}  // namespace

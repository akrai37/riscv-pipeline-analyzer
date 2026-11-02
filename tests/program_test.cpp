// Runs every prebuilt program (the workloads and the riscv-tests suites) and
// requires a clean exit(0). Each program checks its own results.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "rvsim/elf.hpp"
#include "rvsim/memory.hpp"
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

// Guards against a missing or partially built program directory silently
// shrinking the test set.
TEST(PrebuiltPrograms, AllPresent) {
  EXPECT_EQ(elf_files(RVSIM_WORKLOAD_DIR).size(), 7u);
  EXPECT_EQ(elf_files(RVSIM_RISCV_TESTS_DIR).size(), 48u);  // 40 rv32ui + 8 rv32um
}

}  // namespace

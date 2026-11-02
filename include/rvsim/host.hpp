#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "rvsim/memory.hpp"

namespace rvsim {

using RegFile = std::array<uint32_t, 32>;

// The environment a program reaches through ecall. Two Linux-numbered
// syscalls are enough for the workloads and the test environment:
//
//   a7 = 64  write(a0 = fd, a1 = buf, a2 = len) -> len   (fd 1 or 2)
//   a7 = 93  exit(a0 = status)
//
// Output is captured rather than printed so tests and the lockstep checker
// can compare what each model produced.
class Host {
 public:
  // Performs the syscall selected by a7. Returns the value the caller must
  // write back to a0; ignored after exit.
  uint32_t ecall(const RegFile& regs, const Memory& mem);

  bool exited() const { return exited_; }
  int exit_code() const { return exit_code_; }
  const std::string& output() const { return output_; }

 private:
  bool exited_ = false;
  int exit_code_ = 0;
  std::string output_;
};

}  // namespace rvsim

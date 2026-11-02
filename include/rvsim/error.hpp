#pragma once

#include <stdexcept>

namespace rvsim {

// Raised when the simulated program does something the simulator does not
// model or that has no defined result here: an illegal instruction, an
// out-of-range or misaligned access, an unknown syscall, ebreak.
class SimError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

}  // namespace rvsim

#pragma once

#include <cstdint>

namespace rvsim {

// The simulated machine's RAM. Must match MEMORY in workloads/common/link.ld,
// which places code at the base and the initial stack at the top.
inline constexpr uint32_t kRamBase = 0x80000000u;
inline constexpr uint32_t kRamSize = 4u << 20;

}  // namespace rvsim

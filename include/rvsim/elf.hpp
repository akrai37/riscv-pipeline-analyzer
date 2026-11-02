#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "rvsim/memory.hpp"

namespace rvsim {

// A program image that cannot be loaded: not a 32-bit little-endian RISC-V
// executable, truncated, or with segments outside simulated memory.
class LoadError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

std::vector<uint8_t> read_file(const std::string& path);

// Copies every PT_LOAD segment of a static ELF32 RISC-V executable into
// `mem`, zero-filling the part of each segment beyond its file contents
// (.bss). Returns the entry point. Header fields are decoded byte by byte,
// so the loader does not depend on host endianness or struct layout.
uint32_t load_elf(std::span<const uint8_t> image, Memory& mem);

}  // namespace rvsim

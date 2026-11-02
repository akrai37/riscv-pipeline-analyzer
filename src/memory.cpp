#include "rvsim/memory.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

#include "rvsim/error.hpp"

namespace rvsim {

Memory::Memory(uint32_t base, uint32_t size) : base_(base) {
  if (size == 0 || uint64_t{base} + size > (uint64_t{1} << 32)) {
    throw std::invalid_argument("memory region must be non-empty and fit in 32-bit space");
  }
  bytes_.resize(size);
}

bool Memory::contains(uint32_t addr, uint32_t len) const {
  // 64-bit arithmetic so a region ending at 2^32 and an access that wraps
  // past it are both handled.
  return addr >= base_ && uint64_t{addr} + len <= uint64_t{base_} + bytes_.size();
}

std::size_t Memory::offset(uint32_t addr, unsigned len, const char* what) const {
  if (!contains(addr, len)) {
    throw SimError(std::format("{} fault: {}-byte access at {:#010x} outside [{:#010x}, {:#010x})",
                               what, len, addr, base_, uint64_t{base_} + bytes_.size()));
  }
  if (addr % len != 0) {
    throw SimError(std::format("{} fault: misaligned {}-byte access at {:#010x}", what, len, addr));
  }
  return addr - base_;
}

uint32_t Memory::fetch(uint32_t pc) const {
  const std::size_t at = offset(pc, 4, "instruction fetch");
  return uint32_t{bytes_[at]} | uint32_t{bytes_[at + 1]} << 8 | uint32_t{bytes_[at + 2]} << 16 |
         uint32_t{bytes_[at + 3]} << 24;
}

uint32_t Memory::load(uint32_t addr, unsigned len) const {
  const std::size_t at = offset(addr, len, "load");
  uint32_t value = 0;
  for (unsigned i = 0; i < len; ++i) {
    value |= uint32_t{bytes_[at + i]} << (8 * i);
  }
  return value;
}

void Memory::store(uint32_t addr, unsigned len, uint32_t value) {
  const std::size_t at = offset(addr, len, "store");
  for (unsigned i = 0; i < len; ++i) {
    bytes_[at + i] = static_cast<uint8_t>(value >> (8 * i));
  }
}

void Memory::write(uint32_t addr, std::span<const uint8_t> data) {
  if (data.size() > bytes_.size() || !contains(addr, static_cast<uint32_t>(data.size()))) {
    throw SimError(
        std::format("cannot place {} bytes at {:#010x}: outside memory", data.size(), addr));
  }
  std::copy(data.begin(), data.end(), bytes_.begin() + (addr - base_));
}

}  // namespace rvsim

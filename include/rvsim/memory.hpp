#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rvsim {

// Flat little-endian memory covering [base, base + size) with a fixed
// 1-cycle latency. The pipeline reaches it through separate instruction and
// data ports (modified Harvard), so a fetch and a load/store can both happen
// in one cycle without a structural hazard. Self-modifying code is therefore
// unsupported, which is also why fence.i is not implemented.
//
// Accesses must be naturally aligned; a misaligned access raises SimError
// instead of being emulated.
class Memory {
 public:
  Memory(uint32_t base, uint32_t size);

  uint32_t base() const { return base_; }
  uint32_t size() const { return static_cast<uint32_t>(bytes_.size()); }
  bool contains(uint32_t addr, uint32_t len) const;

  uint32_t fetch(uint32_t pc) const;
  uint32_t load(uint32_t addr, unsigned len) const;
  void store(uint32_t addr, unsigned len, uint32_t value);

  // Bulk copy used by the program loader; only bounds are checked.
  void write(uint32_t addr, std::span<const uint8_t> data);

 private:
  std::size_t offset(uint32_t addr, unsigned len, const char* what) const;

  uint32_t base_;
  std::vector<uint8_t> bytes_;
};

}  // namespace rvsim

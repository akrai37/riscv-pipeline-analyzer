#include "rvsim/elf.hpp"

#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>

namespace rvsim {
namespace {

// ELF constants used here (System V ABI, ELF-64 Object File Format v1.5).
constexpr std::size_t kEhdrSize = 52;
constexpr std::size_t kPhdrSize = 32;
constexpr uint8_t kClass32 = 1;
constexpr uint8_t kDataLsb = 1;
constexpr uint16_t kTypeExec = 2;
constexpr uint16_t kMachineRiscv = 243;
constexpr uint32_t kPtLoad = 1;

class Reader {
 public:
  explicit Reader(std::span<const uint8_t> image) : image_(image) {}

  uint16_t u16(std::size_t at) const {
    check(at, 2);
    return static_cast<uint16_t>(image_[at] | image_[at + 1] << 8);
  }

  uint32_t u32(std::size_t at) const {
    check(at, 4);
    return uint32_t{image_[at]} | uint32_t{image_[at + 1]} << 8 | uint32_t{image_[at + 2]} << 16 |
           uint32_t{image_[at + 3]} << 24;
  }

  std::span<const uint8_t> bytes(std::size_t at, std::size_t len) const {
    check(at, len);
    return image_.subspan(at, len);
  }

 private:
  void check(std::size_t at, std::size_t len) const {
    if (at > image_.size() || len > image_.size() - at) {
      throw LoadError(std::format("ELF truncated: need {} bytes at offset {}, file has {}", len, at,
                                  image_.size()));
    }
  }

  std::span<const uint8_t> image_;
};

}  // namespace

std::vector<uint8_t> read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw LoadError(std::format("cannot open '{}'", path));
  }
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

uint32_t load_elf(std::span<const uint8_t> image, Memory& mem) {
  const Reader r(image);
  const auto ident = r.bytes(0, 16);
  if (ident[0] != 0x7f || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') {
    throw LoadError("not an ELF file");
  }
  if (ident[4] != kClass32 || ident[5] != kDataLsb) {
    throw LoadError("not a 32-bit little-endian ELF file");
  }
  if (r.u16(16) != kTypeExec) {
    throw LoadError("not an executable (ET_EXEC) ELF file");
  }
  if (r.u16(18) != kMachineRiscv) {
    throw LoadError(std::format("not a RISC-V ELF file (e_machine = {})", r.u16(18)));
  }

  const uint32_t entry = r.u32(24);
  const uint32_t phoff = r.u32(28);
  const uint16_t phentsize = r.u16(42);
  const uint16_t phnum = r.u16(44);
  if (r.u16(40) < kEhdrSize || (phnum != 0 && phentsize < kPhdrSize)) {
    throw LoadError("malformed ELF header");
  }

  unsigned loaded = 0;
  for (uint16_t i = 0; i < phnum; ++i) {
    const std::size_t ph = std::size_t{phoff} + std::size_t{i} * phentsize;
    if (r.u32(ph) != kPtLoad) {
      continue;
    }
    const uint32_t offset = r.u32(ph + 4);
    const uint32_t vaddr = r.u32(ph + 8);
    const uint32_t filesz = r.u32(ph + 16);
    const uint32_t memsz = r.u32(ph + 20);
    if (filesz > memsz) {
      throw LoadError(std::format("segment {}: file size exceeds memory size", i));
    }
    if (memsz == 0) {
      continue;
    }
    if (!mem.contains(vaddr, memsz)) {
      throw LoadError(std::format("segment {} [{:#010x}, +{:#x}) lies outside simulated memory", i,
                                  vaddr, memsz));
    }
    if (filesz != 0) {
      mem.write(vaddr, r.bytes(offset, filesz));
    }
    // Memory may be reused between runs, so .bss is cleared explicitly.
    const std::vector<uint8_t> zeros(memsz - filesz, 0);
    mem.write(vaddr + filesz, zeros);
    ++loaded;
  }
  if (loaded == 0) {
    throw LoadError("ELF file has no loadable segments");
  }
  if (!mem.contains(entry, 4)) {
    throw LoadError(std::format("entry point {:#010x} lies outside simulated memory", entry));
  }
  return entry;
}

}  // namespace rvsim

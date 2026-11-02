#include "rvsim/elf.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "rvsim/memory.hpp"

namespace {

using rvsim::LoadError;
using rvsim::Memory;

struct Segment {
  uint32_t type = 1;  // PT_LOAD
  uint32_t vaddr;
  std::vector<uint8_t> data;
  uint32_t memsz;
};

void put16(std::vector<uint8_t>& out, std::size_t at, uint16_t v) {
  out[at] = static_cast<uint8_t>(v);
  out[at + 1] = static_cast<uint8_t>(v >> 8);
}

void put32(std::vector<uint8_t>& out, std::size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    out[at + i] = static_cast<uint8_t>(v >> (8 * i));
  }
}

// Builds a minimal ELF32 executable: header, program headers, then each
// segment's file contents in order.
std::vector<uint8_t> make_elf(uint32_t entry, const std::vector<Segment>& segs,
                              uint16_t machine = 243) {
  const std::size_t phoff = 52;
  std::size_t data_off = phoff + 32 * segs.size();
  std::vector<uint8_t> out(data_off, 0);
  out[0] = 0x7f;
  out[1] = 'E';
  out[2] = 'L';
  out[3] = 'F';
  out[4] = 1;         // ELFCLASS32
  out[5] = 1;         // ELFDATA2LSB
  out[6] = 1;         // EV_CURRENT
  put16(out, 16, 2);  // ET_EXEC
  put16(out, 18, machine);
  put32(out, 20, 1);
  put32(out, 24, entry);
  put32(out, 28, static_cast<uint32_t>(phoff));
  put16(out, 40, 52);
  put16(out, 42, 32);
  put16(out, 44, static_cast<uint16_t>(segs.size()));
  for (std::size_t i = 0; i < segs.size(); ++i) {
    const std::size_t ph = phoff + 32 * i;
    put32(out, ph, segs[i].type);
    put32(out, ph + 4, static_cast<uint32_t>(data_off));
    put32(out, ph + 8, segs[i].vaddr);
    put32(out, ph + 12, segs[i].vaddr);
    put32(out, ph + 16, static_cast<uint32_t>(segs[i].data.size()));
    put32(out, ph + 20, segs[i].memsz);
    out.insert(out.end(), segs[i].data.begin(), segs[i].data.end());
    data_off += segs[i].data.size();
  }
  return out;
}

TEST(Elf, LoadsSegmentsAndZeroFillsBss) {
  Memory mem(0x1000, 0x1000);
  mem.store(0x1104, 4, 0xffffffffu);  // stale data where .bss will go
  const auto image = make_elf(0x1000, {
                                          {1, 0x1000, {0x13, 0x00, 0x00, 0x00}, 4},
                                          {1, 0x1100, {0xaa, 0xbb, 0xcc, 0xdd}, 16},
                                      });
  EXPECT_EQ(rvsim::load_elf(image, mem), 0x1000u);
  EXPECT_EQ(mem.load(0x1000, 4), 0x00000013u);
  EXPECT_EQ(mem.load(0x1100, 4), 0xddccbbaau);
  EXPECT_EQ(mem.load(0x1104, 4), 0u);
  EXPECT_EQ(mem.load(0x110c, 4), 0u);
}

TEST(Elf, SkipsNonLoadSegments) {
  Memory mem(0x1000, 0x1000);
  const auto image = make_elf(0x1000, {
                                          {4, 0x9000, {1, 2, 3, 4}, 4},  // PT_NOTE, outside memory
                                          {1, 0x1000, {1, 2, 3, 4}, 4},
                                      });
  EXPECT_NO_THROW(rvsim::load_elf(image, mem));
}

TEST(Elf, RejectsBadImages) {
  Memory mem(0x1000, 0x1000);
  const std::vector<Segment> ok = {{1, 0x1000, {0, 0, 0, 0}, 4}};

  auto bad_magic = make_elf(0x1000, ok);
  bad_magic[1] = 'X';
  EXPECT_THROW(rvsim::load_elf(bad_magic, mem), LoadError);

  auto elf64 = make_elf(0x1000, ok);
  elf64[4] = 2;
  EXPECT_THROW(rvsim::load_elf(elf64, mem), LoadError);

  EXPECT_THROW(rvsim::load_elf(make_elf(0x1000, ok, 62 /* x86-64 */), mem), LoadError);

  auto truncated = make_elf(0x1000, ok);
  truncated.resize(60);  // cuts through the program header
  EXPECT_THROW(rvsim::load_elf(truncated, mem), LoadError);

  EXPECT_THROW(rvsim::load_elf(std::vector<uint8_t>(10, 0), mem), LoadError);
  EXPECT_THROW(rvsim::load_elf(make_elf(0x1000, {{1, 0x1ffc, {0, 0, 0, 0}, 8}}), mem), LoadError);
  EXPECT_THROW(rvsim::load_elf(make_elf(0x1000, {{1, 0x1000, {0, 0, 0, 0}, 2}}), mem), LoadError);
  EXPECT_THROW(rvsim::load_elf(make_elf(0x5000, ok), mem), LoadError);  // entry outside memory
  EXPECT_THROW(rvsim::load_elf(make_elf(0x1000, {}), mem), LoadError);  // nothing to load
}

TEST(Elf, MissingFile) {
  EXPECT_THROW(rvsim::read_file("/nonexistent/program.elf"), LoadError);
}

}  // namespace

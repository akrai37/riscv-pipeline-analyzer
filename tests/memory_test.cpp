#include "rvsim/memory.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "rvsim/error.hpp"

namespace {

using rvsim::Memory;
using rvsim::SimError;

TEST(Memory, LittleEndianLayout) {
  Memory mem(0x1000, 64);
  mem.store(0x1000, 4, 0x11223344u);
  EXPECT_EQ(mem.load(0x1000, 1), 0x44u);
  EXPECT_EQ(mem.load(0x1001, 1), 0x33u);
  EXPECT_EQ(mem.load(0x1002, 2), 0x1122u);
  EXPECT_EQ(mem.fetch(0x1000), 0x11223344u);
}

TEST(Memory, NarrowStoresLeaveNeighboursIntact) {
  Memory mem(0x1000, 64);
  mem.store(0x1000, 4, 0xaabbccddu);
  mem.store(0x1001, 1, 0x00);
  mem.store(0x1002, 2, 0x1234);
  EXPECT_EQ(mem.load(0x1000, 4), 0x123400ddu);
}

TEST(Memory, StartsZeroed) {
  Memory mem(0, 16);
  EXPECT_EQ(mem.load(12, 4), 0u);
}

TEST(Memory, OutOfRangeAccessFaults) {
  Memory mem(0x1000, 64);
  EXPECT_THROW(mem.load(0xffc, 4), SimError);
  EXPECT_THROW(mem.load(0x1040, 1), SimError);
  EXPECT_THROW(mem.store(0x1040, 4, 0), SimError);
  EXPECT_THROW(mem.fetch(0x2000), SimError);
  EXPECT_NO_THROW(mem.load(0x103c, 4));
}

TEST(Memory, MisalignedAccessFaults) {
  Memory mem(0x1000, 64);
  EXPECT_THROW(mem.load(0x1002, 4), SimError);
  EXPECT_THROW(mem.load(0x1001, 2), SimError);
  EXPECT_THROW(mem.store(0x1003, 2, 0), SimError);
  EXPECT_THROW(mem.fetch(0x1002), SimError);
  EXPECT_NO_THROW(mem.load(0x1001, 1));
}

TEST(Memory, RegionEndingAtTopOfAddressSpace) {
  Memory mem(0xfffff000u, 0x1000);
  mem.store(0xfffffffcu, 4, 0xdeadbeefu);
  EXPECT_EQ(mem.load(0xfffffffcu, 4), 0xdeadbeefu);
  EXPECT_FALSE(mem.contains(0xfffffffcu, 8));
  EXPECT_THROW(mem.load(0, 4), SimError);
}

TEST(Memory, RejectsInvalidRegions) {
  EXPECT_THROW(Memory(0, 0), std::invalid_argument);
  EXPECT_THROW(Memory(0xfffff000u, 0x2000), std::invalid_argument);
}

TEST(Memory, BulkWrite) {
  Memory mem(0x100, 16);
  const std::vector<uint8_t> data = {1, 2, 3, 4, 5};
  mem.write(0x103, data);
  EXPECT_EQ(mem.load(0x104, 4), 0x05040302u);
  EXPECT_THROW(mem.write(0x10c, data), SimError);
}

}  // namespace

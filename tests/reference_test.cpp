#include "rvsim/reference.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>

#include "encode.hpp"
#include "rvsim/error.hpp"
#include "rvsim/memory.hpp"

namespace {

using namespace enc;
using rvsim::Memory;
using rvsim::ReferenceModel;
using rvsim::Retired;
using rvsim::SimError;

constexpr uint32_t kBase = 0x1000;
constexpr uint32_t kData = 0x2000;

// Memory with `program` placed at kBase.
Memory load(std::initializer_list<uint32_t> program) {
  Memory mem(kBase, 0x4000);
  uint32_t addr = kBase;
  for (const uint32_t word : program) {
    mem.store(addr, 4, word);
    addr += 4;
  }
  return mem;
}

TEST(Reference, SumLoopAndExit) {
  // a0 = 10 + 9 + ... + 1, then exit(a0).
  Memory mem = load({
      addi(t0, zero, 10),
      addi(a0, zero, 0),
      add(a0, a0, t0),  // loop:
      addi(t0, t0, -1),
      bne(t0, zero, -8),
      addi(a7, zero, 93),
      ecall(),
  });
  ReferenceModel ref(mem, kBase);
  ref.run(1000);
  ASSERT_TRUE(ref.halted());
  EXPECT_EQ(ref.exit_code(), 55);
  EXPECT_EQ(ref.retired(), 2u + 3u * 10u + 2u);
}

TEST(Reference, ZeroRegisterIgnoresWrites) {
  Memory mem = load({addi(zero, zero, 5), add(a0, zero, zero)});
  ReferenceModel ref(mem, kBase);
  const Retired first = ref.step();
  EXPECT_EQ(first.rd, 0);  // a write to x0 is not reported as a write
  EXPECT_EQ(ref.reg(zero), 0u);
  const Retired second = ref.step();
  EXPECT_EQ(second.rd, a0);
  EXPECT_EQ(second.rd_value, 0u);
}

TEST(Reference, LoadsExtendAndStoresTruncate) {
  Memory mem = load({
      lui(s0, kData >> 12),
      addi(t0, zero, -128),  // 0xffffff80
      sb(t0, s0, 0),
      lb(a0, s0, 0),
      lbu(a1, s0, 0),
      sh(t0, s0, 4),
      lh(a2, s0, 4),
      lhu(a3, s0, 4),
      sw(t0, s0, 8),
      lw(a4, s0, 8),
  });
  ReferenceModel ref(mem, kBase);
  ref.step();
  ref.step();
  const Retired store = ref.step();
  EXPECT_EQ(store.rd, 0);
  EXPECT_EQ(store.store_size, 1);
  EXPECT_EQ(store.store_addr, kData);
  EXPECT_EQ(store.store_value, 0x80u);
  ref.run(7);
  EXPECT_EQ(ref.reg(a0), 0xffffff80u);
  EXPECT_EQ(ref.reg(a1), 0x80u);
  EXPECT_EQ(ref.reg(a2), 0xffffff80u);
  EXPECT_EQ(ref.reg(a3), 0xff80u);
  EXPECT_EQ(ref.reg(a4), 0xffffff80u);
  EXPECT_EQ(mem.load(kData + 1, 1), 0u);  // sb wrote one byte only
}

TEST(Reference, CallAndReturn) {
  Memory mem = load({
      jal(ra, 12),         // 0x1000: call f
      addi(a7, zero, 93),  // 0x1004
      ecall(),             // 0x1008
      addi(a0, zero, 42),  // 0x100c: f
      jalr(zero, ra, 0),   // 0x1010: ret
  });
  ReferenceModel ref(mem, kBase);
  const Retired call = ref.step();
  EXPECT_EQ(call.rd, ra);
  EXPECT_EQ(call.rd_value, kBase + 4);
  EXPECT_EQ(ref.pc(), kBase + 12);
  ref.run(10);
  ASSERT_TRUE(ref.halted());
  EXPECT_EQ(ref.exit_code(), 42);
}

TEST(Reference, BackwardAndForwardBranches) {
  Memory mem = load({
      addi(t0, zero, -1),
      blt(t0, zero, 8),   // signed: taken
      addi(a0, zero, 1),  // skipped
      bltu(t0, zero, 8),  // unsigned: 0xffffffff < 0 is false
      addi(a1, zero, 2),  // executed
      addi(a7, zero, 93),
      ecall(),
  });
  ReferenceModel ref(mem, kBase);
  ref.run(100);
  EXPECT_EQ(ref.reg(a0), 0u);
  EXPECT_EQ(ref.reg(a1), 2u);
}

TEST(Reference, MultiplyDivide) {
  Memory mem = load({
      addi(t0, zero, -6),
      addi(t1, zero, 4),
      mul(a0, t0, t1),
      div(a1, t0, t1),
      div(a2, t0, zero),
  });
  ReferenceModel ref(mem, kBase);
  ref.run(5);
  EXPECT_EQ(ref.reg(a0), static_cast<uint32_t>(-24));
  EXPECT_EQ(ref.reg(a1), static_cast<uint32_t>(-1));
  EXPECT_EQ(ref.reg(a2), 0xffffffffu);
}

TEST(Reference, WriteSyscallCapturesOutputAndReturnsLength) {
  Memory mem = load({
      lui(a1, kData >> 12),
      addi(a0, zero, 1),
      addi(a2, zero, 3),
      addi(a7, zero, 64),
      ecall(),
  });
  const std::string msg = "hi\n";
  mem.write(kData, {reinterpret_cast<const uint8_t*>(msg.data()), msg.size()});
  ReferenceModel ref(mem, kBase);
  ref.run(4);
  const Retired ecall_rec = ref.step();
  EXPECT_EQ(ref.host().output(), "hi\n");
  EXPECT_EQ(ecall_rec.rd, a0);
  EXPECT_EQ(ecall_rec.rd_value, 3u);
  EXPECT_FALSE(ref.halted());
}

TEST(Reference, Faults) {
  {
    Memory mem = load({0x00000000});
    ReferenceModel ref(mem, kBase);
    EXPECT_THROW(ref.step(), SimError);
  }
  {
    Memory mem = load({ebreak()});
    ReferenceModel ref(mem, kBase);
    EXPECT_THROW(ref.step(), SimError);
  }
  {
    Memory mem = load({addi(a7, zero, 1), ecall()});
    ReferenceModel ref(mem, kBase);
    ref.step();
    EXPECT_THROW(ref.step(), SimError);  // unknown syscall
  }
  {
    Memory mem = load({lw(a0, zero, 0)});  // address 0 is outside memory
    ReferenceModel ref(mem, kBase);
    EXPECT_THROW(ref.step(), SimError);
  }
  {
    Memory mem = load({addi(a7, zero, 93), ecall()});
    ReferenceModel ref(mem, kBase);
    ref.run(10);
    EXPECT_THROW(ref.step(), SimError);  // stepping after exit
  }
}

TEST(Reference, RunStopsAtInstructionLimit) {
  Memory mem = load({jal(zero, 0)});  // spin forever
  ReferenceModel ref(mem, kBase);
  EXPECT_EQ(ref.run(25), 25u);
  EXPECT_FALSE(ref.halted());
}

}  // namespace

#include "rvsim/predictor.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>

#include "encode.hpp"
#include "rvsim/isa.hpp"

namespace {

using namespace enc;
using rvsim::BranchPredictor;
using rvsim::decode;
using rvsim::PredictorConfig;
using rvsim::RasAction;

TEST(RasAction, FollowsSpecHintTable) {
  EXPECT_EQ(rvsim::ras_action(decode(jal(ra, 16))), RasAction::Push);
  EXPECT_EQ(rvsim::ras_action(decode(jal(t0, 16))), RasAction::Push);  // x5 is a link register
  EXPECT_EQ(rvsim::ras_action(decode(jal(zero, 16))), RasAction::None);
  EXPECT_EQ(rvsim::ras_action(decode(jalr(zero, ra, 0))), RasAction::Pop);  // ret
  EXPECT_EQ(rvsim::ras_action(decode(jalr(ra, a0, 0))), RasAction::Push);   // indirect call
  EXPECT_EQ(rvsim::ras_action(decode(jalr(ra, ra, 0))), RasAction::Push);   // rd == rs1
  EXPECT_EQ(rvsim::ras_action(decode(jalr(ra, t0, 0))), RasAction::PopPush);
  EXPECT_EQ(rvsim::ras_action(decode(jalr(zero, a0, 0))), RasAction::None);  // indirect jump
  EXPECT_EQ(rvsim::ras_action(decode(beq(a0, a1, 8))), RasAction::None);
}

TEST(Predictor, ColdBtbFallsThrough) {
  BranchPredictor p({});
  const auto pred = p.predict(0x100);
  EXPECT_FALSE(pred.btb_hit);
  EXPECT_EQ(pred.next_pc, 0x104u);
}

TEST(Predictor, TwoBitCountersSaturate) {
  BranchPredictor p({});
  const auto br = decode(bne(a0, zero, -16));
  // Taken three times: counter goes 1 -> 2 -> 3 -> 3.
  for (int i = 0; i < 3; ++i) {
    const auto pred = p.predict(0x100);
    p.resolve(br, 0x100, true, 0xf0, pred.next_pc != 0xf0, pred);
  }
  EXPECT_EQ(p.predict(0x100).next_pc, 0xf0u);
  // One not-taken outcome only weakens it (3 -> 2): still predicted taken.
  auto pred = p.predict(0x100);
  p.resolve(br, 0x100, false, 0xf0, true, pred);
  EXPECT_EQ(p.predict(0x100).next_pc, 0xf0u);
  // A second one flips it (2 -> 1).
  pred = p.predict(0x100);
  p.resolve(br, 0x100, false, 0xf0, true, pred);
  EXPECT_EQ(p.predict(0x100).next_pc, 0x104u);
}

// Addresses below are chosen so no two map to the same slot of the default
// 64-entry direct-mapped BTB (index = (pc >> 2) & 63).

TEST(Predictor, BtfnUsesBtbTargetDirection) {
  PredictorConfig c;
  c.direction = rvsim::DirectionPredictor::Btfn;
  BranchPredictor p(c);
  const auto backward = decode(bne(a0, zero, -16));
  const auto forward = decode(beq(a0, zero, 32));
  auto pred = p.predict(0x100);
  p.resolve(backward, 0x100, true, 0xf0, true, pred);
  pred = p.predict(0x208);
  p.resolve(forward, 0x208, true, 0x228, true, pred);
  EXPECT_EQ(p.predict(0x100).next_pc, 0xf0u);   // backward: taken
  EXPECT_EQ(p.predict(0x208).next_pc, 0x20cu);  // forward: not taken
}

TEST(Predictor, ReturnUsesRasAndFallsBackToBtbTarget) {
  BranchPredictor p({});
  const auto call = decode(jal(ra, 0x100));
  const auto ret = decode(jalr(zero, ra, 0));
  // Train: call at 0x10 -> f at 0x180, return to 0x14.
  auto pred = p.predict(0x10);
  p.resolve(call, 0x10, true, 0x180, true, pred);
  pred = p.predict(0x180);
  p.resolve(ret, 0x180, true, 0x14, true, pred);

  // A call from a new site at 0x40: a BTB miss, so the push happens when
  // the misprediction is repaired.
  pred = p.predict(0x40);
  p.resolve(call, 0x40, true, 0x180, true, pred);
  EXPECT_EQ(p.predict(0x180).next_pc, 0x44u);  // popped from the RAS
  EXPECT_EQ(p.predict(0x180).next_pc, 0x14u);  // RAS empty: last BTB target
}

TEST(Predictor, MispredictRestoresRasCheckpoint) {
  BranchPredictor p({});
  const auto call = decode(jal(ra, 0x100));
  const auto ret = decode(jalr(zero, ra, 0));
  // Put a call at 0x88 and the return at 0x180 into the BTB, leaving the
  // RAS empty.
  auto pred = p.predict(0x88);
  p.resolve(call, 0x88, true, 0x180, true, pred);
  pred = p.predict(0x180);
  p.resolve(ret, 0x180, true, 0x8c, true, pred);

  // Real call at 0x10: RAS = [0x14].
  pred = p.predict(0x10);
  p.resolve(call, 0x10, true, 0x180, true, pred);

  // A branch at 0x20 is predicted not-taken, and the wrong path behind it
  // reaches the call at 0x88, which pushes 0x8c speculatively.
  const auto br = decode(beq(a0, a1, 64));
  const auto branch_pred = p.predict(0x20);
  p.predict(0x88);
  // The branch was actually taken: the RAS goes back to [0x14].
  p.resolve(br, 0x20, true, 0x60, true, branch_pred);
  EXPECT_EQ(p.predict(0x180).next_pc, 0x14u);  // 0x8c without the restore
}

TEST(Predictor, RasOverflowOverwritesOldest) {
  PredictorConfig c;
  c.ras_entries = 2;
  BranchPredictor p(c);
  const auto call = decode(jal(ra, 0x100));
  const auto ret = decode(jalr(zero, ra, 0));
  for (uint32_t site : {0x10u, 0x20u, 0x30u}) {
    const auto pred = p.predict(site);
    p.resolve(call, site, true, 0x200, true, pred);
  }
  // Three pushes into two entries: [0x24, 0x34]. The first return misses the
  // BTB and its repair pops 0x34.
  const auto pred = p.predict(0x200);
  p.resolve(ret, 0x200, true, 0x34, true, pred);
  EXPECT_EQ(p.predict(0x200).next_pc, 0x24u);
  // With 0x14 overwritten the stack is now empty: BTB fallback.
  EXPECT_EQ(p.predict(0x200).next_pc, 0x34u);
}

TEST(Predictor, RejectsBadConfigs) {
  PredictorConfig c;
  c.btb_entries = 48;
  EXPECT_THROW(BranchPredictor{c}, std::invalid_argument);
  c = {};
  c.pht_entries = 0;
  EXPECT_THROW(BranchPredictor{c}, std::invalid_argument);
  c = {};
  c.ras_entries = 17;
  EXPECT_THROW(BranchPredictor{c}, std::invalid_argument);
}

}  // namespace

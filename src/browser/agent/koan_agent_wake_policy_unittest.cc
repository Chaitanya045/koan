// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_wake_policy.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {

namespace {

const base::TimeTicks kStart = base::TimeTicks() + base::Seconds(100);
constexpr PowerState kSaving{.on_battery = true, .low_power_mode = true};

base::TimeTicks At(double seconds) {
  return kStart + base::Seconds(seconds);
}

WakeInputs Working(bool keep_awake, PowerState power = {}) {
  return {
      .leased = true, .active = true, .keep_awake = keep_awake, .power = power};
}

WakeInputs Idle(PowerState power = {}) {
  return {.leased = true, .active = false, .power = power};
}

}  // namespace

TEST(KoanAgentWakePolicyTest, WhatEachStateWants) {
  EXPECT_EQ(WakeBlockFor({}), WakeBlock::kNone);
  // No agent connected: nothing, whatever else is set.
  EXPECT_EQ(WakeBlockFor({.leased = false, .active = true, .keep_awake = true}),
            WakeBlock::kNone);
  EXPECT_EQ(WakeBlockFor(Idle()), WakeBlock::kNone);
  EXPECT_EQ(WakeBlockFor(Working(false)), WakeBlock::kAppSuspension);
  EXPECT_EQ(WakeBlockFor(Working(true)), WakeBlock::kDisplaySleep);
  // Low Power Mode on battery: never the display.
  EXPECT_EQ(WakeBlockFor(Working(true, kSaving)), WakeBlock::kAppSuspension);
  EXPECT_EQ(WakeBlockFor(Working(false, kSaving)), WakeBlock::kAppSuspension);
  // Battery alone, or Low Power Mode alone (plugged in), changes nothing.
  EXPECT_EQ(WakeBlockFor(Working(true, {.on_battery = true})),
            WakeBlock::kDisplaySleep);
  EXPECT_EQ(WakeBlockFor(Working(true, {.low_power_mode = true})),
            WakeBlock::kDisplaySleep);
}

// Released 3 s after the hold ends; work that resumes meanwhile keeps it.
TEST(KoanAgentWakePolicyTest, ReleasedAfterTheHoldOff) {
  WakePolicy policy;
  EXPECT_EQ(policy.Update(Working(true), At(0)), WakeBlock::kDisplaySleep);
  EXPECT_EQ(policy.Update(Working(true), At(20)), WakeBlock::kDisplaySleep);
  EXPECT_EQ(policy.Update(Idle(), At(21)), WakeBlock::kDisplaySleep);
  EXPECT_EQ(policy.Update(Idle(), At(22.9)), WakeBlock::kDisplaySleep);
  EXPECT_EQ(policy.Update(Idle(), At(23)), WakeBlock::kNone);
  EXPECT_EQ(policy.held(), WakeBlock::kNone);

  // Back to work within the hold-off: held throughout.
  EXPECT_EQ(policy.Update(Working(false), At(30)), WakeBlock::kAppSuspension);
  EXPECT_EQ(policy.Update(Idle(), At(31)), WakeBlock::kAppSuspension);
  EXPECT_EQ(policy.Update(Working(false), At(32)), WakeBlock::kAppSuspension);
  EXPECT_EQ(policy.Update(Idle(), At(34.5)), WakeBlock::kAppSuspension);
  EXPECT_EQ(policy.Update(Idle(), At(35)), WakeBlock::kNone);
}

// A keep-awake client that stops while another agent works: the display is
// let go after the hold-off, idle sleep stays prevented.
TEST(KoanAgentWakePolicyTest, StepsDownAfterTheHoldOff) {
  WakePolicy policy;
  policy.Update(Working(true), At(0));
  EXPECT_EQ(policy.Update(Working(false), At(1)), WakeBlock::kDisplaySleep);
  EXPECT_EQ(policy.Update(Working(false), At(3)), WakeBlock::kAppSuspension);
  // And up again at once.
  EXPECT_EQ(policy.Update(Working(true), At(4)), WakeBlock::kDisplaySleep);
}

// No lease left (the agent disconnected, Stop, Agent ready off): dropped at
// once, no hold-off.
TEST(KoanAgentWakePolicyTest, NoAgentDropsAtOnce) {
  WakePolicy policy;
  policy.Update(Working(true), At(0));
  EXPECT_EQ(policy.Update({}, At(0.5)), WakeBlock::kNone);
}

// Entering Low Power Mode on battery lets the display go at once; idle sleep
// is still prevented while the agent works and for the hold-off after.
TEST(KoanAgentWakePolicyTest, LowPowerModeLetsTheDisplayGoAtOnce) {
  WakePolicy policy;
  policy.Update(Working(true), At(0));
  EXPECT_EQ(policy.Update(Working(true, kSaving), At(1)),
            WakeBlock::kAppSuspension);
  EXPECT_EQ(policy.Update(Idle(kSaving), At(2)), WakeBlock::kAppSuspension);
  EXPECT_EQ(policy.Update(Idle(kSaving), At(4)), WakeBlock::kNone);

  // Entering it after the work ended, within the hold-off.
  WakePolicy late;
  late.Update(Working(true), At(0));
  late.Update(Idle(), At(1));
  EXPECT_EQ(late.Update(Idle(kSaving), At(2)), WakeBlock::kAppSuspension);
  EXPECT_EQ(late.Update(Idle(kSaving), At(3)), WakeBlock::kNone);
  // After the hold-off: nothing.
  WakePolicy later;
  later.Update(Working(true), At(0));
  EXPECT_EQ(later.Update(Idle(kSaving), At(3)), WakeBlock::kNone);

  // Leaving it gives the display back to a keep-awake agent at once.
  EXPECT_EQ(policy.Update(Working(true, kSaving), At(10)),
            WakeBlock::kAppSuspension);
  EXPECT_EQ(policy.Update(Working(true), At(11)), WakeBlock::kDisplaySleep);
}

}  // namespace koan::agent

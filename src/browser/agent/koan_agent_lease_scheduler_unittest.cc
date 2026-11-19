// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_lease_scheduler.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {

namespace {

using Strings = std::vector<std::string>;

const base::TimeTicks kStart = base::TimeTicks() + base::Seconds(100);
constexpr PowerState kSaving{.on_battery = true, .low_power_mode = true};

}  // namespace

// T-keep-9: held while a command runs and for 3 s after, then released, and
// taken again by the next command.
TEST(KoanAgentLeaseSchedulerTest, TheHoldOff) {
  LeaseScheduler leases(4, true, {});
  LeaseScheduler::Changes changes;
  EXPECT_TRUE(leases.BeginCommand("a", changes));
  EXPECT_EQ(changes.acquire, Strings{"a"});
  leases.EndCommand("a", kStart);
  EXPECT_TRUE(leases.Tick(kStart + base::Seconds(2)).empty());
  EXPECT_TRUE(leases.IsCapturing("a"));
  EXPECT_EQ(leases.Tick(kStart + base::Seconds(3)).release, Strings{"a"});
  EXPECT_FALSE(leases.IsCapturing("a"));

  LeaseScheduler::Changes again;
  EXPECT_TRUE(leases.BeginCommand("a", again));
  EXPECT_EQ(again.acquire, Strings{"a"});
  // A long command keeps it, whatever the clock says.
  EXPECT_TRUE(leases.Tick(kStart + base::Minutes(5)).empty());
}

TEST(KoanAgentLeaseSchedulerTest, HoldsAndKeepAlive) {
  LeaseScheduler leases(4, true, {});
  LeaseScheduler::Changes changes;
  EXPECT_TRUE(leases.BeginHold("a", changes));
  EXPECT_TRUE(leases.Tick(kStart + base::Minutes(30)).empty());
  leases.EndHold("a", kStart + base::Minutes(30));
  EXPECT_EQ(leases.Tick(kStart + base::Minutes(31)).release, Strings{"a"});

  LeaseScheduler::Changes keep;
  EXPECT_FALSE(leases.KeepAlive("b", "k", base::Minutes(2), kStart, keep));
  EXPECT_EQ(keep.acquire, Strings{"b"});
  EXPECT_TRUE(leases.Tick(kStart + base::Seconds(119)).empty());
  EXPECT_EQ(leases.Tick(kStart + base::Seconds(120)).release, Strings{"b"});
  // At most ten minutes.
  LeaseScheduler::Changes longer;
  leases.KeepAlive("c", "k", base::Hours(1), kStart, longer);
  EXPECT_EQ(leases.Tick(kStart + base::Minutes(10)).release, Strings{"c"});
}

// T-limits-2: over the limit a tab waits; nothing held is dropped when the
// limit is lowered, and a raised limit lets the queue in.
TEST(KoanAgentLeaseSchedulerTest, TheLimitQueuesAndNeverDrops) {
  LeaseScheduler leases(2, true, {});
  LeaseScheduler::Changes changes;
  EXPECT_TRUE(leases.BeginCommand("a", changes));
  EXPECT_TRUE(leases.BeginCommand("b", changes));
  EXPECT_FALSE(leases.BeginCommand("c", changes));
  EXPECT_TRUE(leases.IsQueued("c"));
  EXPECT_EQ(changes.acquire, (Strings{"a", "b"}));

  // Lowered to 1: a and b keep theirs.
  EXPECT_TRUE(leases.SetLimit(1, true, kStart).empty());
  EXPECT_EQ(leases.capturing_count(), 2u);
  leases.EndCommand("a", kStart);
  EXPECT_EQ(leases.Tick(kStart + base::Seconds(3)).release, Strings{"a"});
  // Still one over the new limit of 1: c waits.
  EXPECT_TRUE(leases.IsQueued("c"));
  // Raised again: c runs.
  EXPECT_EQ(leases.SetLimit(2, true, kStart).acquire, Strings{"c"});
  EXPECT_FALSE(leases.IsQueued("c"));
}

TEST(KoanAgentLeaseSchedulerTest, ADetachGivesItsPlaceOn) {
  LeaseScheduler leases(1, true, {});
  LeaseScheduler::Changes changes;
  leases.BeginCommand("a", changes);
  EXPECT_FALSE(leases.BeginCommand("b", changes));
  const LeaseScheduler::Changes detached = leases.Detach("a");
  EXPECT_EQ(detached.release, Strings{"a"});
  EXPECT_EQ(detached.acquire, Strings{"b"});
  // A queued tab that detaches leaves the queue.
  LeaseScheduler::Changes more;
  EXPECT_FALSE(leases.BeginCommand("c", more));
  EXPECT_TRUE(leases.Detach("c").empty());
  EXPECT_FALSE(leases.IsQueued("c"));
  EXPECT_TRUE(leases.Detach("unknown").empty());
}

// T-power-2 and T-power-3.
TEST(KoanAgentLeaseSchedulerTest, LowPowerModeOnBattery) {
  LeaseScheduler leases(4, true, {});
  LeaseScheduler::Changes changes;
  ASSERT_FALSE(leases.SetLiveAlways("live", true, changes));
  for (const char* tab : {"a", "b", "c"}) {
    leases.BeginCommand(tab, changes);
  }
  EXPECT_EQ(leases.capturing_count(), 4u);

  // Entering the state drops nothing, and downgrades `live: always`.
  EXPECT_TRUE(leases.SetPower(kSaving, kStart).empty());
  EXPECT_EQ(leases.limit(), 2);
  EXPECT_EQ(leases.capturing_count(), 4u);
  EXPECT_EQ(leases.Tick(kStart + base::Seconds(3)).release, Strings{"live"});

  LeaseScheduler::Changes refused;
  const auto long_keep =
      leases.KeepAlive("d", "k", base::Seconds(61), kStart, refused);
  ASSERT_TRUE(long_keep);
  EXPECT_EQ(long_keep->max_leases, 2);
  EXPECT_EQ(long_keep->max_keep_alive_seconds, 60);
  EXPECT_FALSE(leases.KeepAlive("d", "k", base::Seconds(60), kStart, refused));
  // Over the halved limit: queued, not failed.
  EXPECT_TRUE(leases.IsQueued("d"));
  EXPECT_TRUE(leases.SetLiveAlways("e", true, refused).has_value());

  // Leaving restores the limit, and the queue runs.
  EXPECT_EQ(leases.SetPower({}, kStart).acquire, Strings{"d"});
  EXPECT_FALSE(leases.SetLiveAlways("e", true, refused).has_value());
}

TEST(KoanAgentLeaseSchedulerTest, OnlyBatteryOrOnlyLowPowerChangesNothing) {
  for (PowerState power :
       {PowerState{.on_battery = true}, PowerState{.low_power_mode = true}}) {
    LeaseScheduler leases(4, true, power);
    EXPECT_EQ(leases.limit(), 4);
    LeaseScheduler::Changes changes;
    EXPECT_FALSE(leases.KeepAlive("a", "k", base::Minutes(5), kStart, changes));
  }
  // With the halving off the limit stays, but the refusals still apply.
  LeaseScheduler leases(4, false, kSaving);
  EXPECT_EQ(leases.limit(), 4);
  LeaseScheduler::Changes changes;
  EXPECT_TRUE(leases.KeepAlive("a", "k", base::Seconds(61), kStart, changes));
  // A setting of 1 halves to 1, not 0.
  LeaseScheduler one(1, true, kSaving);
  EXPECT_EQ(one.limit(), 1);
}

// Each keep-alive has its own key: two waits on a tab do not end each other's,
// a renewal moves the end, and an ended one starts the hold-off.
TEST(KoanAgentLeaseSchedulerTest, KeepAlivesAreKeyed) {
  LeaseScheduler leases(4, true, {});
  LeaseScheduler::Changes changes;
  EXPECT_FALSE(leases.KeepAlive("a", "w1", base::Seconds(30), kStart, changes));
  EXPECT_FALSE(leases.KeepAlive("a", "w2", base::Seconds(30), kStart, changes));
  EXPECT_EQ(changes.acquire, Strings{"a"});
  EXPECT_TRUE(leases.IsActive("a", kStart));
  // w1 ends: w2 still holds it.
  LeaseScheduler::Changes none;
  EXPECT_FALSE(leases.KeepAlive("a", "w1", base::TimeDelta(),
                                kStart + base::Seconds(5), none));
  EXPECT_TRUE(leases.Tick(kStart + base::Seconds(20)).empty());
  // w2 renewed at 25 s for 30 s more: held until 55 s.
  EXPECT_FALSE(leases.KeepAlive("a", "w2", base::Seconds(30),
                                kStart + base::Seconds(25), none));
  EXPECT_TRUE(leases.Tick(kStart + base::Seconds(54)).empty());
  EXPECT_EQ(leases.Tick(kStart + base::Seconds(55)).release, Strings{"a"});
  EXPECT_FALSE(leases.IsActive("a", kStart + base::Seconds(55)));

  // Ended early: the hold-off, then released.
  LeaseScheduler::Changes again;
  leases.KeepAlive("a", "w3", base::Minutes(5), kStart + base::Seconds(60),
                   again);
  leases.KeepAlive("a", "w3", base::TimeDelta(), kStart + base::Seconds(61),
                   again);
  EXPECT_FALSE(leases.IsActive("a", kStart + base::Seconds(62)));
  EXPECT_TRUE(leases.Tick(kStart + base::Seconds(63)).empty());
  EXPECT_EQ(leases.Tick(kStart + base::Seconds(64)).release, Strings{"a"});
  // Ending an unknown key, or on an unknown tab, does nothing.
  EXPECT_FALSE(leases.KeepAlive("a", "w9", base::TimeDelta(), kStart, none));
  EXPECT_FALSE(leases.KeepAlive("z", "w9", base::TimeDelta(), kStart, none));
  EXPECT_FALSE(leases.IsCapturing("z"));
}

// Holds are counted: the capturer stays until the last one ends.
TEST(KoanAgentLeaseSchedulerTest, HoldsAreCounted) {
  LeaseScheduler leases(4, true, {});
  LeaseScheduler::Changes changes;
  EXPECT_TRUE(leases.BeginHold("a", changes));
  EXPECT_TRUE(leases.BeginHold("a", changes));
  EXPECT_EQ(changes.acquire, Strings{"a"});
  leases.EndHold("a", kStart);
  EXPECT_TRUE(leases.IsActive("a", kStart + base::Minutes(1)));
  EXPECT_TRUE(leases.Tick(kStart + base::Minutes(1)).empty());
  leases.EndHold("a", kStart + base::Minutes(1));
  EXPECT_FALSE(leases.IsActive("a", kStart + base::Minutes(1)));
  EXPECT_EQ(leases.Tick(kStart + base::Minutes(1) + base::Seconds(3)).release,
            Strings{"a"});
  // An extra EndHold() never goes below zero, so it cannot cancel a later
  // hold.
  leases.EndHold("a", kStart);
  LeaseScheduler::Changes later;
  EXPECT_TRUE(leases.BeginHold("a", later));
  EXPECT_TRUE(leases.Tick(kStart + base::Hours(1)).empty());
}

// The cap applies to holds and keep-alives as to commands: over it they
// queue, and they get the capturer as others release it.
TEST(KoanAgentLeaseSchedulerTest, HoldsQueueAtTheCap) {
  LeaseScheduler leases(4, true, kSaving);
  ASSERT_EQ(leases.limit(), 2);
  LeaseScheduler::Changes changes;
  EXPECT_TRUE(leases.BeginHold("a", changes));
  EXPECT_FALSE(leases.KeepAlive("b", "w", base::Seconds(30), kStart, changes));
  EXPECT_FALSE(leases.BeginHold("c", changes));
  EXPECT_TRUE(leases.IsQueued("c"));
  EXPECT_FALSE(leases.KeepAlive("d", "w", base::Seconds(60), kStart, changes));
  EXPECT_TRUE(leases.IsQueued("d"));
  EXPECT_EQ(changes.acquire, (Strings{"a", "b"}));
  // A queued tab is not at work yet (no assertion for it).
  EXPECT_FALSE(leases.IsActive("c", kStart));

  // a's screencast stops: after the hold-off c gets its place.
  leases.EndHold("a", kStart);
  const LeaseScheduler::Changes swap = leases.Tick(kStart + base::Seconds(3));
  EXPECT_EQ(swap.release, Strings{"a"});
  EXPECT_EQ(swap.acquire, Strings{"c"});
  // b's keep-alive runs out: d's place.
  const LeaseScheduler::Changes next = leases.Tick(kStart + base::Seconds(30));
  EXPECT_EQ(next.release, Strings{"b"});
  EXPECT_EQ(next.acquire, Strings{"d"});
}

// Every release path ends in Detach() (tab closed, detached, the client gone
// or revoked, Stop, Agent ready off): it drops the tab's holds, keep-alives
// and commands at once, with no hold-off, and a later lease of the tab starts
// clean.
TEST(KoanAgentLeaseSchedulerTest, DetachDropsEveryHold) {
  LeaseScheduler leases(4, true, {});
  LeaseScheduler::Changes changes;
  leases.BeginHold("a", changes);
  leases.BeginHold("a", changes);
  leases.BeginCommand("a", changes);
  leases.KeepAlive("a", "w", base::Minutes(10), kStart, changes);
  EXPECT_EQ(leases.Detach("a").release, Strings{"a"});
  EXPECT_FALSE(leases.IsCapturing("a"));
  EXPECT_FALSE(leases.IsActive("a", kStart));
  EXPECT_TRUE(leases.Tick(kStart + base::Seconds(1)).empty());
  // Ends that come after the detach do not bring it back.
  leases.EndHold("a", kStart);
  leases.EndCommand("a", kStart);
  EXPECT_FALSE(leases.KeepAlive("a", "w", base::TimeDelta(), kStart, changes));
  EXPECT_FALSE(leases.IsCapturing("a"));
  EXPECT_EQ(leases.capturing_count(), 0u);
  // Attached again: one command is one command.
  LeaseScheduler::Changes fresh;
  leases.BeginCommand("a", fresh);
  leases.EndCommand("a", kStart);
  EXPECT_EQ(leases.Tick(kStart + base::Seconds(3)).release, Strings{"a"});
}

// What the power assertion follows: a command, a hold or a keep-alive, never
// the hold-off or `live: always` alone.
TEST(KoanAgentLeaseSchedulerTest, ActiveIsWorkNotTheHoldOff) {
  LeaseScheduler leases(4, true, {});
  LeaseScheduler::Changes changes;
  leases.BeginCommand("a", changes);
  EXPECT_TRUE(leases.IsActive("a", kStart));
  leases.EndCommand("a", kStart);
  EXPECT_TRUE(leases.IsCapturing("a"));
  EXPECT_FALSE(leases.IsActive("a", kStart));
  leases.SetLiveAlways("b", true, changes);
  EXPECT_TRUE(leases.IsCapturing("b"));
  EXPECT_FALSE(leases.IsActive("b", kStart));
  EXPECT_FALSE(leases.IsActive("unknown", kStart));
}

// Entering Low Power Mode on battery cuts long keep-alives to 60 s from then.
TEST(KoanAgentLeaseSchedulerTest, LowPowerModeShortensKeepAlives) {
  LeaseScheduler leases(4, true, {});
  LeaseScheduler::Changes changes;
  leases.KeepAlive("a", "w", base::Minutes(10), kStart, changes);
  leases.SetPower(kSaving, kStart);
  EXPECT_TRUE(leases.Tick(kStart + base::Seconds(59)).empty());
  EXPECT_EQ(leases.Tick(kStart + base::Seconds(60)).release, Strings{"a"});
}

}  // namespace koan::agent

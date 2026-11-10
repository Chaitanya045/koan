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
  EXPECT_FALSE(leases.KeepAlive("b", base::Minutes(2), kStart, keep));
  EXPECT_EQ(keep.acquire, Strings{"b"});
  EXPECT_TRUE(leases.Tick(kStart + base::Seconds(119)).empty());
  EXPECT_EQ(leases.Tick(kStart + base::Seconds(120)).release, Strings{"b"});
  // At most ten minutes.
  LeaseScheduler::Changes longer;
  leases.KeepAlive("c", base::Hours(1), kStart, longer);
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
      leases.KeepAlive("d", base::Seconds(61), kStart, refused);
  ASSERT_TRUE(long_keep);
  EXPECT_EQ(long_keep->max_leases, 2);
  EXPECT_EQ(long_keep->max_keep_alive_seconds, 60);
  EXPECT_FALSE(leases.KeepAlive("d", base::Seconds(60), kStart, refused));
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
    EXPECT_FALSE(leases.KeepAlive("a", base::Minutes(5), kStart, changes));
  }
  // With the halving off the limit stays, but the refusals still apply.
  LeaseScheduler leases(4, false, kSaving);
  EXPECT_EQ(leases.limit(), 4);
  LeaseScheduler::Changes changes;
  EXPECT_TRUE(leases.KeepAlive("a", base::Seconds(61), kStart, changes));
  // A setting of 1 halves to 1, not 0.
  LeaseScheduler one(1, true, kSaving);
  EXPECT_EQ(one.limit(), 1);
}

}  // namespace koan::agent

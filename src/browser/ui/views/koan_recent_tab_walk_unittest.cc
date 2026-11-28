// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_recent_tab_walk.h"

#include <optional>
#include <set>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {

// Tabs by name; the list is most recently used first.
using Walk = RecentTabWalk<char>;

constexpr auto kAll = [](char) { return true; };

// One Ctrl+Tab as KoanShortcutController::CycleToRecentTab() takes it: a new
// walk over `recent` unless this press continues one, then a step. Returns
// the tab it reaches (the new active one).
char Press(Walk& walk,
           char active,
           bool control_down,
           const std::vector<char>& recent,
           bool forward = true) {
  if (!walk.Continues(active, control_down)) {
    walk.Start(recent);
    forward = true;
  }
  const std::optional<char> reached = walk.Step(forward, kAll);
  return reached.value_or(active);
}

// Tabs A, B, C visited in that order (C active), Ctrl held for three presses
// of Tab: B, then A, then round to C. Letting go of Ctrl ends the walk.
TEST(KoanRecentTabWalkTest, WalksWhileControlIsHeldAndEndsOnRelease) {
  Walk walk;
  const std::vector<char> recent = {'C', 'B', 'A'};
  EXPECT_EQ(Press(walk, 'C', true, recent), 'B');
  EXPECT_EQ(Press(walk, 'B', true, recent), 'A');
  EXPECT_EQ(Press(walk, 'A', true, recent), 'C');
  EXPECT_TRUE(walk.IsWalking());

  walk.End();
  EXPECT_FALSE(walk.IsWalking());
  EXPECT_FALSE(walk.Continues('C', true));
}

// After the walk ended on B, B is the most recent tab: the next Ctrl+Tab
// starts again and flips back to C.
TEST(KoanRecentTabWalkTest, ReachedTabIsMostRecentForTheNextWalk) {
  Walk walk;
  EXPECT_EQ(Press(walk, 'C', true, {'C', 'B', 'A'}), 'B');
  walk.End();
  EXPECT_EQ(Press(walk, 'B', true, {'B', 'C', 'A'}), 'C');
}

// With Ctrl up between presses (each press is a Ctrl+Tab of its own), every
// press starts a new walk: only the previous tab is reached.
TEST(KoanRecentTabWalkTest, ReleasedControlStartsANewWalk) {
  Walk walk;
  EXPECT_EQ(Press(walk, 'C', true, {'C', 'B', 'A'}), 'B');
  EXPECT_FALSE(walk.Continues('B', false));
  EXPECT_EQ(Press(walk, 'B', false, {'B', 'C', 'A'}), 'C');
}

// Another tab activated meanwhile (a click in the sidebar) starts a new walk.
TEST(KoanRecentTabWalkTest, OtherActiveTabStartsANewWalk) {
  Walk walk;
  EXPECT_EQ(Press(walk, 'C', true, {'C', 'B', 'A'}), 'B');
  EXPECT_FALSE(walk.Continues('A', true));
  EXPECT_FALSE(walk.Continues(std::nullopt, true));
  EXPECT_TRUE(walk.Continues('B', true));
}

// Shift goes back up the list; the first press of a walk goes forward.
TEST(KoanRecentTabWalkTest, ShiftGoesBack) {
  Walk walk;
  const std::vector<char> recent = {'D', 'C', 'B', 'A'};
  EXPECT_EQ(Press(walk, 'D', true, recent, /*forward=*/false), 'C');
  EXPECT_EQ(Press(walk, 'C', true, recent), 'B');
  EXPECT_EQ(Press(walk, 'B', true, recent, /*forward=*/false), 'C');
  EXPECT_EQ(Press(walk, 'C', true, recent, /*forward=*/false), 'D');
  EXPECT_EQ(Press(walk, 'D', true, recent, /*forward=*/false), 'A');
}

// A tab closed since the walk began is passed over.
TEST(KoanRecentTabWalkTest, PassesOverClosedTabs) {
  Walk walk;
  walk.Start({'C', 'B', 'A'});
  std::set<char> open = {'C', 'A'};
  auto exists = [&open](char tab) { return open.contains(tab); };
  EXPECT_EQ(walk.Step(true, exists), 'A');
  EXPECT_TRUE(walk.Continues('A', true));
  EXPECT_EQ(walk.Step(true, exists), 'C');
}

// Nothing to go to: the only tab, or all the others closed.
TEST(KoanRecentTabWalkTest, NothingToReach) {
  Walk walk;
  walk.Start({'A'});
  EXPECT_EQ(walk.Step(true, kAll), std::nullopt);
  walk.Start({'B', 'A'});
  EXPECT_EQ(walk.Step(true, [](char tab) { return tab == 'B'; }), std::nullopt);
}

}  // namespace

}  // namespace koan

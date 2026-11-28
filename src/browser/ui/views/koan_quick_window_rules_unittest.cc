// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_quick_window_rules.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::quick_window {
namespace {

// A link that opens in a quick window; each test changes one fact.
LinkFacts QuickLink() {
  LinkFacts facts;
  facts.quick_window_chosen = true;
  facts.web_url = true;
  facts.has_window = true;
  return facts;
}

TEST(KoanQuickWindowRulesTest, UnroutedWebLinkOpensInQuickWindow) {
  EXPECT_TRUE(OpensInQuickWindow(QuickLink()));
}

TEST(KoanQuickWindowRulesTest, SpaceRoutingIsTheDefault) {
  LinkFacts facts = QuickLink();
  facts.quick_window_chosen = false;
  EXPECT_FALSE(OpensInQuickWindow(facts));
}

TEST(KoanQuickWindowRulesTest, RouteWins) {
  LinkFacts facts = QuickLink();
  facts.routed = true;
  EXPECT_FALSE(OpensInQuickWindow(facts));
}

TEST(KoanQuickWindowRulesTest, IncognitoUnchanged) {
  LinkFacts facts = QuickLink();
  facts.off_the_record = true;
  EXPECT_FALSE(OpensInQuickWindow(facts));
}

TEST(KoanQuickWindowRulesTest, OnlyWebPages) {
  LinkFacts facts = QuickLink();
  facts.web_url = false;
  EXPECT_FALSE(OpensInQuickWindow(facts));
}

TEST(KoanQuickWindowRulesTest, NeedsAWindowToComeBackTo) {
  LinkFacts facts = QuickLink();
  facts.has_window = false;
  EXPECT_FALSE(OpensInQuickWindow(facts));
}

// macOS closes a window's only new tab page after the links open: the link
// takes that window instead.
TEST(KoanQuickWindowRulesTest, OnlyNewTabPageTakesTheLink) {
  LinkFacts facts = QuickLink();
  facts.window_is_only_new_tab = true;
  EXPECT_FALSE(OpensInQuickWindow(facts));
}

TEST(KoanQuickWindowRulesTest, BoundsAreCentredShares) {
  const gfx::Rect bounds = QuickWindowBounds(gfx::Rect(0, 25, 1440, 875));
  EXPECT_EQ(bounds.width(), 864);   // 60%
  EXPECT_EQ(bounds.height(), 656);  // 75%
  EXPECT_EQ(bounds.x(), (1440 - 864) / 2);
  EXPECT_EQ(bounds.y(), 25 + (875 - 656) / 2);
}

TEST(KoanQuickWindowRulesTest, BoundsAreClamped) {
  const gfx::Rect large = QuickWindowBounds(gfx::Rect(0, 0, 3840, 2160));
  EXPECT_EQ(large.size(), gfx::Size(1000, 840));
  const gfx::Rect small = QuickWindowBounds(gfx::Rect(0, 0, 640, 400));
  EXPECT_EQ(small.size(), gfx::Size(480, 360));
  // Never larger than the work area.
  const gfx::Rect tiny = QuickWindowBounds(gfx::Rect(10, 10, 300, 200));
  EXPECT_EQ(tiny, gfx::Rect(10, 10, 300, 200));
}

TEST(KoanQuickWindowRulesTest, MoveMenuListsTheMostRecentSpaceFirst) {
  EXPECT_EQ(MoveMenuOrder(4, 2), (std::vector<size_t>{2, 0, 1, 3}));
  EXPECT_EQ(MoveMenuOrder(3, 0), (std::vector<size_t>{0, 1, 2}));
  EXPECT_EQ(MoveMenuOrder(3, std::nullopt), (std::vector<size_t>{0, 1, 2}));
  // An active index out of range is ignored.
  EXPECT_EQ(MoveMenuOrder(2, 5), (std::vector<size_t>{0, 1}));
  EXPECT_TRUE(MoveMenuOrder(0, std::nullopt).empty());
}

}  // namespace
}  // namespace koan::quick_window

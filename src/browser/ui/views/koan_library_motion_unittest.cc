// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenLibrary.mjs (the values the tests check)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// What the Library's open progress does to the panel and the sidebar, with
// compact mode on and off, and Zen's spring.

#include "koan/browser/ui/views/koan_library_motion.h"

#include <algorithm>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {

constexpr float kPanel = 418;
constexpr float kSidebar = 230;

}  // namespace

TEST(KoanLibraryMotionTest, SpringStartsAtRestAndSettlesAtTheTarget) {
  EXPECT_EQ(LibrarySpring(0), 0);
  EXPECT_EQ(LibrarySpring(-1), 0);
  // Slightly under-damped: past 1 a little, then settled by 0.45 s.
  double peak = 0;
  for (int ms = 0; ms <= 450; ms += 5) {
    peak = std::max(peak, LibrarySpring(ms / 1000.0));
  }
  EXPECT_GT(peak, 1);
  EXPECT_LT(peak, 1.02);
  EXPECT_NEAR(LibrarySpring(0.45), 1, 0.002);
  // Half way within the first tenth of a second.
  EXPECT_GT(LibrarySpring(0.1), 0.5);
}

TEST(KoanLibraryMotionTest, CompactModeCollapsesTheSidebarFirst) {
  const LibraryMotion closed = GetLibraryMotion(0, kPanel, kSidebar, true);
  EXPECT_EQ(closed.panel_offset, kPanel);
  EXPECT_EQ(closed.sidebar_offset, 0);
  // The sidebar is out of the way by a third of the progress, while the
  // panel has only come a third of the way in.
  const LibraryMotion third = GetLibraryMotion(1.f / 3, kPanel, kSidebar, true);
  EXPECT_FLOAT_EQ(third.sidebar_offset, 1);
  EXPECT_FLOAT_EQ(third.panel_offset, kPanel * 2 / 3);
  const LibraryMotion sixth = GetLibraryMotion(1.f / 6, kPanel, kSidebar, true);
  EXPECT_FLOAT_EQ(sixth.sidebar_offset, 0.5f);
  const LibraryMotion open = GetLibraryMotion(1, kPanel, kSidebar, true);
  EXPECT_EQ(open.panel_offset, 0);
  EXPECT_EQ(open.sidebar_offset, 1);
  // It slides; it doesn't fade or shrink, nor does the panel fade.
  for (const LibraryMotion& motion : {closed, sixth, third, open}) {
    EXPECT_EQ(motion.sidebar_opacity, 1);
    EXPECT_EQ(motion.sidebar_scale, 1);
    EXPECT_EQ(motion.panel_opacity, 1);
  }
}

TEST(KoanLibraryMotionTest, WithoutCompactModeTheSidebarGrowsIntoThePanel) {
  // Closed: the panel's far edge is at the sidebar's, unseen.
  const LibraryMotion closed = GetLibraryMotion(0, kPanel, kSidebar, false);
  EXPECT_EQ(closed.panel_offset, kPanel - kSidebar);
  EXPECT_EQ(closed.panel_opacity, 0);
  EXPECT_EQ(closed.sidebar_opacity, 1);
  EXPECT_EQ(closed.sidebar_scale, 1);
  EXPECT_EQ(closed.sidebar_offset, 0);
  // The edge moves out with the progress, as Zen's page edge does.
  const LibraryMotion half = GetLibraryMotion(0.5f, kPanel, kSidebar, false);
  EXPECT_FLOAT_EQ(half.panel_offset, (kPanel - kSidebar) / 2);
  // The sidebar has faded into the panel by a third of the way.
  const LibraryMotion sixth =
      GetLibraryMotion(1.f / 6, kPanel, kSidebar, false);
  EXPECT_FLOAT_EQ(sixth.sidebar_opacity, 0.5f);
  EXPECT_FLOAT_EQ(sixth.panel_opacity, 0.5f);
  EXPECT_FLOAT_EQ(sixth.sidebar_scale, 0.98f);
  EXPECT_EQ(half.sidebar_opacity, 0);
  EXPECT_EQ(half.panel_opacity, 1);
  EXPECT_FLOAT_EQ(half.sidebar_scale, 0.96f);
  const LibraryMotion open = GetLibraryMotion(1, kPanel, kSidebar, false);
  EXPECT_EQ(open.panel_offset, 0);
  EXPECT_EQ(open.sidebar_offset, 0);
}

TEST(KoanLibraryMotionTest, RubberBandAndEdgeCases) {
  // Past closed (a swipe's rubber band): the sidebar stays as it is.
  const LibraryMotion past_closed =
      GetLibraryMotion(-0.05f, kPanel, kSidebar, false);
  EXPECT_EQ(past_closed.sidebar_opacity, 1);
  EXPECT_EQ(past_closed.sidebar_scale, 1);
  EXPECT_EQ(past_closed.panel_opacity, 0);
  EXPECT_EQ(GetLibraryMotion(-0.05f, kPanel, kSidebar, true).sidebar_offset, 0);
  // Past open, the panel comes a little further in.
  EXPECT_LT(GetLibraryMotion(1.05f, kPanel, kSidebar, false).panel_offset, 0);
  // No sidebar shown: the panel slides in from the edge.
  const LibraryMotion no_sidebar = GetLibraryMotion(0.5f, kPanel, 0, false);
  EXPECT_FLOAT_EQ(no_sidebar.panel_offset, kPanel / 2);
  EXPECT_EQ(no_sidebar.panel_opacity, 1);
  // A sidebar as wide as the panel: it only cross-fades.
  EXPECT_EQ(GetLibraryMotion(0.5f, kPanel, kPanel + 10, false).panel_offset, 0);
}

}  // namespace koan

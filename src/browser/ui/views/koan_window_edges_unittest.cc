// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_window_edges.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {

// A window at (100, 50), 800 by 600, in screen coordinates.
gfx::Rect Window() {
  return gfx::Rect(100, 50, 800, 600);
}

}  // namespace

TEST(KoanWindowEdgesTest, APointerFlungPastAnEdgeLeftThroughIt) {
  // Mid-height, far to the left of the window (a fast move): clamped, it is
  // on the left edge.
  EXPECT_EQ(WindowEdge::kLeft, CrossedWindowEdge(Window(), {-300, 350}));
  EXPECT_EQ(WindowEdge::kLeft, CrossedWindowEdge(Window(), {105, 350}));
  EXPECT_EQ(WindowEdge::kRight, CrossedWindowEdge(Window(), {1400, 350}));
  EXPECT_EQ(WindowEdge::kTop, CrossedWindowEdge(Window(), {500, 0}));
  EXPECT_EQ(WindowEdge::kBottom, CrossedWindowEdge(Window(), {500, 900}));
}

TEST(KoanWindowEdgesTest, APointerAwayFromTheEdgesLeftThroughNone) {
  // (A leave reported while the pointer is still well inside, e.g. onto a
  // child window.)
  EXPECT_EQ(WindowEdge::kNone, CrossedWindowEdge(Window(), {500, 350}));
  EXPECT_EQ(WindowEdge::kNone, CrossedWindowEdge(Window(), {111, 350}));
  EXPECT_EQ(WindowEdge::kNone, CrossedWindowEdge(gfx::Rect(), {0, 0}));
}

TEST(KoanWindowEdgesTest, ACornerCountsAsTheTopOrTheBottom) {
  // Zen tries top, bottom, left, right in that order.
  EXPECT_EQ(WindowEdge::kTop, CrossedWindowEdge(Window(), {-50, 0}));
  EXPECT_EQ(WindowEdge::kBottom, CrossedWindowEdge(Window(), {-50, 900}));
  EXPECT_EQ(WindowEdge::kTop, CrossedWindowEdge(Window(), {1000, 55}));
}

TEST(KoanWindowEdgesTest, AlongTheElementWithTheTolerance) {
  // The sidebar, inset 10 at the top and bottom.
  const gfx::Rect sidebar(100, 60, 246, 580);
  EXPECT_TRUE(IsAlongElement(sidebar, WindowEdge::kLeft, {90, 300}));
  EXPECT_TRUE(IsAlongElement(sidebar, WindowEdge::kLeft, {90, 54}));
  EXPECT_FALSE(IsAlongElement(sidebar, WindowEdge::kLeft, {90, 53}));
  EXPECT_TRUE(IsAlongElement(sidebar, WindowEdge::kLeft, {90, 646}));
  EXPECT_FALSE(IsAlongElement(sidebar, WindowEdge::kLeft, {90, 647}));
  // The toolbar, along the top.
  const gfx::Rect toolbar(108, 50, 784, 40);
  EXPECT_TRUE(IsAlongElement(toolbar, WindowEdge::kTop, {500, 20}));
  EXPECT_FALSE(IsAlongElement(toolbar, WindowEdge::kTop, {100, 20}));
  EXPECT_FALSE(IsAlongElement(toolbar, WindowEdge::kNone, {500, 20}));
}

TEST(KoanWindowEdgesTest, TheSidebarStaysWithin200PointsLeftOfTheWindow) {
  constexpr int kOffset = kOutsideWindowHorizontalOffset;
  EXPECT_EQ(200, kOffset);
  EXPECT_TRUE(IsWithinOutsideTolerance(Window(), WindowEdge::kLeft, kOffset,
                                       {99, 300}));
  EXPECT_TRUE(IsWithinOutsideTolerance(Window(), WindowEdge::kLeft, kOffset,
                                       {-100, 300}));
  EXPECT_FALSE(IsWithinOutsideTolerance(Window(), WindowEdge::kLeft, kOffset,
                                        {-101, 300}));
  // Above or below the window's extent.
  EXPECT_FALSE(
      IsWithinOutsideTolerance(Window(), WindowEdge::kLeft, kOffset, {50, 49}));
  EXPECT_FALSE(IsWithinOutsideTolerance(Window(), WindowEdge::kLeft, kOffset,
                                        {50, 651}));
  // Past the other edges.
  EXPECT_FALSE(IsWithinOutsideTolerance(Window(), WindowEdge::kLeft, kOffset,
                                        {950, 300}));
  EXPECT_FALSE(IsWithinOutsideTolerance(Window(), WindowEdge::kLeft, kOffset,
                                        {500, 20}));
  // Back over the window.
  EXPECT_TRUE(IsWithinOutsideTolerance(Window(), WindowEdge::kLeft, kOffset,
                                       {500, 300}));
}

TEST(KoanWindowEdgesTest, TheOtherEdges) {
  EXPECT_TRUE(IsWithinOutsideTolerance(Window(), WindowEdge::kRight,
                                       kOutsideWindowHorizontalOffset,
                                       {1100, 300}));
  EXPECT_FALSE(IsWithinOutsideTolerance(Window(), WindowEdge::kRight,
                                        kOutsideWindowHorizontalOffset,
                                        {1101, 300}));
  constexpr int kTop = kOutsideWindowVerticalOffset;
  EXPECT_EQ(100, kTop);
  EXPECT_TRUE(
      IsWithinOutsideTolerance(Window(), WindowEdge::kTop, kTop, {500, -50}));
  EXPECT_FALSE(
      IsWithinOutsideTolerance(Window(), WindowEdge::kTop, kTop, {500, -51}));
  EXPECT_FALSE(
      IsWithinOutsideTolerance(Window(), WindowEdge::kTop, kTop, {950, 20}));
  EXPECT_FALSE(
      IsWithinOutsideTolerance(Window(), WindowEdge::kNone, kTop, {500, 20}));
}

}  // namespace koan

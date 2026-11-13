// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_split_rearrange.h"

#include "components/split_tabs/split_tab_visual_data.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

namespace {

using split_tabs::SplitTabLayout;
using split_tabs::SplitTabVisualData;

// A 1000 x 500 pane at (100, 50): its outer 24% is 240 wide on the left
// and right, 120 high on the top and bottom.
constexpr gfx::RectF kPane(100, 50, 1000, 500);

}  // namespace

TEST(KoanSplitRearrangeTest, OuterQuarterOfEachEdgeIsThatSide) {
  EXPECT_EQ(RearrangeSide::kLeft, RearrangeSideAt(kPane, {100, 300}));
  EXPECT_EQ(RearrangeSide::kLeft, RearrangeSideAt(kPane, {340, 300}));
  EXPECT_EQ(RearrangeSide::kCenter, RearrangeSideAt(kPane, {341, 300}));
  EXPECT_EQ(RearrangeSide::kCenter, RearrangeSideAt(kPane, {860, 300}));
  EXPECT_EQ(RearrangeSide::kRight, RearrangeSideAt(kPane, {861, 300}));
  EXPECT_EQ(RearrangeSide::kTop, RearrangeSideAt(kPane, {600, 170}));
  EXPECT_EQ(RearrangeSide::kCenter, RearrangeSideAt(kPane, {600, 171}));
  EXPECT_EQ(RearrangeSide::kCenter, RearrangeSideAt(kPane, {600, 430}));
  EXPECT_EQ(RearrangeSide::kBottom, RearrangeSideAt(kPane, {600, 431}));
}

TEST(KoanSplitRearrangeTest, CornersAreLeftOrRightDrops) {
  // Zen tests the left and right edges before the top and bottom.
  EXPECT_EQ(RearrangeSide::kLeft, RearrangeSideAt(kPane, {110, 60}));
  EXPECT_EQ(RearrangeSide::kRight, RearrangeSideAt(kPane, {1090, 540}));
}

TEST(KoanSplitRearrangeTest, DropZoneIsThePaneOrItsHalfOnThatSide) {
  EXPECT_EQ(kPane, RearrangeDropZone(kPane, RearrangeSide::kCenter));
  EXPECT_EQ(gfx::RectF(100, 50, 500, 500),
            RearrangeDropZone(kPane, RearrangeSide::kLeft));
  EXPECT_EQ(gfx::RectF(600, 50, 500, 500),
            RearrangeDropZone(kPane, RearrangeSide::kRight));
  EXPECT_EQ(gfx::RectF(100, 50, 1000, 250),
            RearrangeDropZone(kPane, RearrangeSide::kTop));
  EXPECT_EQ(gfx::RectF(100, 300, 1000, 250),
            RearrangeDropZone(kPane, RearrangeSide::kBottom));
}

TEST(KoanSplitRearrangeTest, CentreSwapsAndEachSlotKeepsItsSize) {
  // Zen's swapNodes() swaps the panes' sizes along with them: the first
  // slot stays 30% wide, now with the other tab.
  const SplitTabVisualData current(SplitTabLayout::kSideBySide, 0.3);
  for (int dragged : {0, 1}) {
    EXPECT_EQ((RearrangeResult{.reverse = true, .visual_data = current}),
              RearrangeSplit(current, dragged, RearrangeSide::kCenter,
                             /*rtl=*/false));
  }
  const SplitTabVisualData stacked(SplitTabLayout::kStacked, 0.7);
  EXPECT_EQ((RearrangeResult{.reverse = true, .visual_data = stacked}),
            RearrangeSplit(stacked, 1, RearrangeSide::kCenter, true));
}

TEST(KoanSplitRearrangeTest, EdgePutsTheDraggedPaneOnThatSideAtHalf) {
  const SplitTabVisualData side_by_side(SplitTabLayout::kSideBySide, 0.3);
  const SplitTabVisualData half_side_by_side(SplitTabLayout::kSideBySide, 0.5);
  const SplitTabVisualData half_stacked(SplitTabLayout::kStacked, 0.5);
  // The first (left) pane dropped on the right one's right edge: swapped.
  EXPECT_EQ(
      (RearrangeResult{.reverse = true, .visual_data = half_side_by_side}),
      RearrangeSplit(side_by_side, 0, RearrangeSide::kRight, false));
  // On its left edge: the same order, at half.
  EXPECT_EQ(
      (RearrangeResult{.reverse = false, .visual_data = half_side_by_side}),
      RearrangeSplit(side_by_side, 0, RearrangeSide::kLeft, false));
  // On its top edge: stacked, the dragged pane on top.
  EXPECT_EQ((RearrangeResult{.reverse = false, .visual_data = half_stacked}),
            RearrangeSplit(side_by_side, 0, RearrangeSide::kTop, false));
  // The second pane on the first one's top edge: stacked, it goes first.
  EXPECT_EQ((RearrangeResult{.reverse = true, .visual_data = half_stacked}),
            RearrangeSplit(side_by_side, 1, RearrangeSide::kTop, false));
  EXPECT_EQ((RearrangeResult{.reverse = false, .visual_data = half_stacked}),
            RearrangeSplit(side_by_side, 1, RearrangeSide::kBottom, false));
  // Stacked to side by side.
  const SplitTabVisualData stacked(SplitTabLayout::kStacked, 0.6);
  EXPECT_EQ(
      (RearrangeResult{.reverse = true, .visual_data = half_side_by_side}),
      RearrangeSplit(stacked, 1, RearrangeSide::kLeft, false));
}

TEST(KoanSplitRearrangeTest, RightToLeftPutsTheFirstPaneOnTheRight) {
  const SplitTabVisualData half_side_by_side(SplitTabLayout::kSideBySide, 0.5);
  const SplitTabVisualData stacked(SplitTabLayout::kStacked, 0.5);
  // The first pane (on the right) dropped on the left edge of the other:
  // it becomes the second.
  EXPECT_EQ(
      (RearrangeResult{.reverse = true, .visual_data = half_side_by_side}),
      RearrangeSplit(half_side_by_side, 0, RearrangeSide::kLeft, true));
  EXPECT_EQ(
      (RearrangeResult{.reverse = false, .visual_data = half_side_by_side}),
      RearrangeSplit(half_side_by_side, 0, RearrangeSide::kRight, true));
  // Top and bottom don't mirror.
  EXPECT_EQ((RearrangeResult{.reverse = false, .visual_data = stacked}),
            RearrangeSplit(half_side_by_side, 0, RearrangeSide::kTop, true));
}

}  // namespace koan

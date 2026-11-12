// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_drag_logic.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

TEST(KoanSpaceDragLogicTest, EdgesSwitchWithinTheirPadding) {
  EXPECT_EQ(-1, SpaceDragEdge(0, 240));
  EXPECT_EQ(-1, SpaceDragEdge(20, 240));
  EXPECT_EQ(-1, SpaceDragEdge(-20, 240));
  EXPECT_EQ(0, SpaceDragEdge(21, 240));
  EXPECT_EQ(0, SpaceDragEdge(120, 240));
  EXPECT_EQ(1, SpaceDragEdge(220, 240));
  EXPECT_EQ(1, SpaceDragEdge(260, 240));
  EXPECT_EQ(0, SpaceDragEdge(261, 240));
}

TEST(KoanSpaceDragLogicTest, ReorderGoesBeforeOrAfterTheButtonUnderIt) {
  // Three buttons 20 wide.
  const std::vector<SpaceButtonSpan> spans = {{0, 20}, {20, 40}, {40, 60}};
  // Dragging the first over the second's first half: still first.
  EXPECT_EQ(0u, ReorderedSpaceIndex(spans, 0, 25));
  // Over its second half: after it.
  EXPECT_EQ(1u, ReorderedSpaceIndex(spans, 0, 35));
  // Over the last's second half: last.
  EXPECT_EQ(2u, ReorderedSpaceIndex(spans, 0, 55));
  // The last over the first's first half: first.
  EXPECT_EQ(0u, ReorderedSpaceIndex(spans, 2, 5));
  // The last over the second's second half: stays.
  EXPECT_EQ(2u, ReorderedSpaceIndex(spans, 2, 35));
  // Over itself or between buttons: stays.
  EXPECT_EQ(1u, ReorderedSpaceIndex(spans, 1, 30));
  EXPECT_EQ(1u, ReorderedSpaceIndex(spans, 1, 80));
}

TEST(KoanSpaceDragLogicTest, StripCentresButtonsThatFit) {
  // Three Spaces in a 200px strip: full compact width (17), centred.
  const SpaceStripLayout layout = LayoutSpaceStrip(200, 3, 10, 17);
  EXPECT_EQ(17, layout.button_width);
  EXPECT_EQ((200 - 51) / 2, layout.start);
  EXPECT_EQ(0, layout.max_scroll);
  // Twelve in 150px: narrower (12), still fitting.
  const SpaceStripLayout narrower = LayoutSpaceStrip(150, 12, 10, 17);
  EXPECT_EQ(12, narrower.button_width);
  EXPECT_EQ(3, narrower.start);
  EXPECT_EQ(0, narrower.max_scroll);
  EXPECT_EQ(0, LayoutSpaceStrip(150, 0, 10, 17).max_scroll);
}

// The bug: 30 Spaces at the 10px minimum are 300px, wider than the strip;
// they ran past "+" and the sidebar. Now they start at the strip's start and
// the rest scrolls.
TEST(KoanSpaceDragLogicTest, StripScrollsSpacesThatDontFit) {
  const SpaceStripLayout layout = LayoutSpaceStrip(180, 30, 10, 17);
  EXPECT_EQ(10, layout.button_width);
  EXPECT_EQ(0, layout.start);
  EXPECT_EQ(300 - 180, layout.max_scroll);
  // No room at all: everything scrolls.
  EXPECT_EQ(300, LayoutSpaceStrip(-5, 30, 10, 17).max_scroll);
}

TEST(KoanSpaceDragLogicTest, ScrollIntoViewMovesAsLittleAsItCan) {
  constexpr int kRoom = 180;
  constexpr int kMax = 120;
  // Already in view (with its 20px margin): no move.
  EXPECT_EQ(50, ScrollSpaceIntoView(50, kRoom, kMax, 100, 110));
  // Cut off at the end: just enough to show it and its margin.
  EXPECT_EQ(250 + 20 - kRoom, ScrollSpaceIntoView(0, kRoom, kMax, 240, 250));
  // Cut off at the start.
  EXPECT_EQ(30 - 20, ScrollSpaceIntoView(100, kRoom, kMax, 30, 40));
  // Near the ends the margin stops at the scroll range.
  EXPECT_EQ(0, ScrollSpaceIntoView(100, kRoom, kMax, 0, 10));
  EXPECT_EQ(kMax, ScrollSpaceIntoView(0, kRoom, kMax, 290, 300));
  // Nothing to scroll.
  EXPECT_EQ(0, ScrollSpaceIntoView(0, kRoom, 0, 290, 300));
}

// For every Space of an overflowing strip, scrolling it into view leaves the
// whole button inside the strip.
TEST(KoanSpaceDragLogicTest, EverySpaceCanBeScrolledIntoView) {
  constexpr int kRoom = 180;
  const SpaceStripLayout layout = LayoutSpaceStrip(kRoom, 30, 10, 17);
  int scroll = 0;
  for (int i : {29, 0, 15, 28, 1}) {
    const int start = layout.start + i * layout.button_width;
    const int end = start + layout.button_width;
    scroll = ScrollSpaceIntoView(scroll, kRoom, layout.max_scroll, start, end);
    EXPECT_GE(start - scroll, 0) << i;
    EXPECT_LE(end - scroll, kRoom) << i;
  }
}

TEST(KoanSpaceDragLogicTest, StripScrollSettlesLikeFirefox) {
  EXPECT_EQ(0, SpaceStripScrollProgress(base::TimeDelta()));
  const double early = SpaceStripScrollProgress(base::Milliseconds(50));
  const double later = SpaceStripScrollProgress(base::Milliseconds(200));
  EXPECT_GT(early, 0);
  EXPECT_GT(later, early);
  EXPECT_GT(SpaceStripScrollProgress(base::Milliseconds(590)), 0.999);
  EXPECT_EQ(1, SpaceStripScrollProgress(kSpaceStripScrollDuration));
}

// Zen's Essentials grid: a 2nd, 3rd... tab goes where it is dropped. Two
// rows of tiles 50 wide and 44 high with 4 between them: [0 1 2] [3].
TEST(KoanSpaceDragLogicTest, EssentialsDropGoesBeforeOrAfterTheTileUnderIt) {
  const std::vector<gfx::Rect> tiles = {
      {0, 0, 50, 44}, {54, 0, 50, 44}, {108, 0, 50, 44}, {0, 48, 50, 44}};
  // The first tile's left half: first; its right half: second.
  EXPECT_EQ(0u, EssentialsDropSlot(tiles, {10, 20}, /*rtl=*/false));
  EXPECT_EQ(1u, EssentialsDropSlot(tiles, {40, 20}, /*rtl=*/false));
  // Between the second and third tiles, on the third's near half.
  EXPECT_EQ(2u, EssentialsDropSlot(tiles, {110, 20}, /*rtl=*/false));
  // The last tile of the first row, its far half: before the second row.
  EXPECT_EQ(3u, EssentialsDropSlot(tiles, {150, 20}, /*rtl=*/false));
  // The second row's tile.
  EXPECT_EQ(3u, EssentialsDropSlot(tiles, {5, 60}, /*rtl=*/false));
  EXPECT_EQ(4u, EssentialsDropSlot(tiles, {45, 60}, /*rtl=*/false));
  // Empty grid space (beside the last tile, in a gap): after the last.
  EXPECT_EQ(4u, EssentialsDropSlot(tiles, {120, 60}, /*rtl=*/false));
  EXPECT_EQ(4u, EssentialsDropSlot(tiles, {52, 20}, /*rtl=*/false));
  // No tiles yet (the promo): the first.
  EXPECT_EQ(0u, EssentialsDropSlot({}, {10, 10}, /*rtl=*/false));
}

TEST(KoanSpaceDragLogicTest, EssentialsDropFromAboveTheGridGoesFirst) {
  // The grid at 132 in the sidebar, [0 1 2] [3]: a tab dragged straight up
  // over the URL bar (98), or the gap above the grid, goes before the first
  // tile, wherever it is across.
  const std::vector<gfx::Rect> tiles = {{0, 132, 50, 44},
                                        {54, 132, 50, 44},
                                        {108, 132, 50, 44},
                                        {0, 180, 50, 44}};
  EXPECT_EQ(0u, EssentialsDropSlot(tiles, {150, 98}, /*rtl=*/false));
  EXPECT_EQ(0u, EssentialsDropSlot(tiles, {200, 131}, /*rtl=*/false));
  EXPECT_EQ(0u, EssentialsDropSlot(tiles, {10, 98}, /*rtl=*/true));
  // The grid's own rules from its top on.
  EXPECT_EQ(3u, EssentialsDropSlot(tiles, {150, 132}, /*rtl=*/false));
  EXPECT_EQ(4u, EssentialsDropSlot(tiles, {200, 150}, /*rtl=*/false));
}

TEST(KoanSpaceDragLogicTest, EssentialsTopZoneIsTheSidebarAboveTheGrid) {
  const gfx::Size sidebar(230, 800);
  const gfx::Rect zone =
      EssentialsTopDropZone(sidebar, gfx::Rect(0, 132, 230, 92));
  EXPECT_EQ(gfx::Rect(0, 0, 230, 132), zone);
  // The URL pill and the window buttons' row; not the grid, which has its
  // own rules, or past the sidebar's side.
  EXPECT_TRUE(zone.Contains(115, 98));
  EXPECT_TRUE(zone.Contains(5, 10));
  EXPECT_FALSE(zone.Contains(115, 132));
  EXPECT_FALSE(zone.Contains(230, 98));
  // No Essentials (the grid 0 high): nothing; the promo's zone does it.
  EXPECT_TRUE(
      EssentialsTopDropZone(sidebar, gfx::Rect(0, 132, 230, 0)).IsEmpty());
  // A grid scrolled above the sidebar's top: nothing.
  EXPECT_TRUE(
      EssentialsTopDropZone(sidebar, gfx::Rect(0, -40, 230, 92)).IsEmpty());
}

TEST(KoanSpaceDragLogicTest, EssentialsDropMirrorsInRtl) {
  // RTL: the first tile is on the right.
  const std::vector<gfx::Rect> tiles = {{108, 0, 50, 44}, {54, 0, 50, 44}};
  // The first tile's right half is its near half.
  EXPECT_EQ(0u, EssentialsDropSlot(tiles, {150, 20}, /*rtl=*/true));
  EXPECT_EQ(1u, EssentialsDropSlot(tiles, {115, 20}, /*rtl=*/true));
  EXPECT_EQ(2u, EssentialsDropSlot(tiles, {60, 20}, /*rtl=*/true));
}

TEST(KoanSpaceDragLogicTest, SpaceListDropGoesAboveOrBelowTheRowUnderIt) {
  // Three rows 36 high with 2 between them.
  const std::vector<gfx::Rect> rows = {
      {0, 0, 200, 36}, {0, 38, 200, 36}, {0, 76, 200, 36}};
  EXPECT_EQ(0u, SpaceListDropSlot(rows, -10));
  EXPECT_EQ(0u, SpaceListDropSlot(rows, 10));
  EXPECT_EQ(1u, SpaceListDropSlot(rows, 30));
  // In the gap: below the first row's middle, above the second's.
  EXPECT_EQ(1u, SpaceListDropSlot(rows, 37));
  EXPECT_EQ(2u, SpaceListDropSlot(rows, 60));
  EXPECT_EQ(3u, SpaceListDropSlot(rows, 100));
  EXPECT_EQ(3u, SpaceListDropSlot(rows, 400));
  EXPECT_EQ(0u, SpaceListDropSlot({}, 10));
}

TEST(KoanSpaceDragLogicTest, EmptyEssentialsZoneIsTheTopOfTheSidebar) {
  // The Mac's layout: a sidebar 230 wide, the URL pill at 78-124, the Space
  // header at 132-176.
  const gfx::Size sidebar(230, 800);
  const gfx::Rect header(0, 132, 230, 44);
  const gfx::Rect before = EmptyEssentialsDropZone(sidebar, header, {});
  EXPECT_EQ(gfx::Rect(0, 0, 230, 154), before);
  // Over the URL pill, the window buttons' row and the header's top half.
  EXPECT_TRUE(before.Contains(115, 98));
  EXPECT_TRUE(before.Contains(5, 10));
  EXPECT_TRUE(before.Contains(115, 153));
  // Not its bottom half, the Space's tabs, or past the sidebar's side.
  EXPECT_FALSE(before.Contains(115, 154));
  EXPECT_FALSE(before.Contains(115, 300));
  EXPECT_FALSE(before.Contains(230, 98));
  EXPECT_FALSE(before.Contains(-1, 98));
}

TEST(KoanSpaceDragLogicTest, EmptyEssentialsZoneEndsAtThePromo) {
  const gfx::Size sidebar(230, 800);
  // The promo (132-224) pushed the header down.
  const gfx::Rect zone = EmptyEssentialsDropZone(
      sidebar, gfx::Rect(0, 224, 230, 44), gfx::Rect(0, 132, 230, 92));
  EXPECT_EQ(gfx::Rect(0, 0, 230, 224), zone);
  EXPECT_TRUE(zone.Contains(115, 98));
  EXPECT_TRUE(zone.Contains(115, 223));
  // The header, below it, is the Space's again.
  EXPECT_FALSE(zone.Contains(115, 230));
  // Added but not laid out yet: the header's middle still.
  EXPECT_EQ(gfx::Rect(0, 0, 230, 154),
            EmptyEssentialsDropZone(sidebar, gfx::Rect(0, 132, 230, 44),
                                    gfx::Rect(0, 0, 0, 0)));
}

TEST(KoanSpaceDragLogicTest, EmptyEssentialsZoneStaysInTheSidebar) {
  // A header scrolled or laid out out of sight: nothing, or no more than the
  // sidebar.
  EXPECT_TRUE(
      EmptyEssentialsDropZone({230, 800}, gfx::Rect(0, -60, 230, 44), {})
          .IsEmpty());
  EXPECT_EQ(
      gfx::Rect(0, 0, 230, 400),
      EmptyEssentialsDropZone({230, 400}, gfx::Rect(0, 900, 230, 44), {}));
}

}  // namespace koan

// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_drag_logic.h"

#include <optional>
#include <vector>

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

// A Space's list as Koan lays it out: two pinned rows (0-36, 38-74), the
// separator (76-98), the New Tab row (98-134), two normal rows (136-172,
// 174-210).
namespace {
std::vector<gfx::Rect> SpaceRows() {
  return {
      {0, 0, 200, 36}, {0, 38, 200, 36}, {0, 136, 200, 36}, {0, 174, 200, 36}};
}
constexpr gfx::Rect kSeparator(0, 76, 200, 22);
constexpr int kNormalStart = 134;
}  // namespace

TEST(KoanSpaceDragLogicTest, PinnedSectionEndsAtTheSeparatorsBottom) {
  // The Space header and the pinned rows, and the separator (Zen keeps it in
  // the pinned section).
  EXPECT_TRUE(IsInPinnedDropSection(-30, kSeparator.bottom()));
  EXPECT_TRUE(IsInPinnedDropSection(50, kSeparator.bottom()));
  EXPECT_TRUE(IsInPinnedDropSection(97, kSeparator.bottom()));
  // The New Tab row, the normal rows and the empty space below.
  EXPECT_FALSE(IsInPinnedDropSection(98, kSeparator.bottom()));
  EXPECT_FALSE(IsInPinnedDropSection(150, kSeparator.bottom()));
  EXPECT_FALSE(IsInPinnedDropSection(600, kSeparator.bottom()));
}

namespace {

SpaceListRow TabRow(const gfx::Rect& bounds, bool pinned, int folder = 0) {
  SpaceListRow row;
  row.bounds = bounds;
  row.pinned = pinned;
  row.folder = folder;
  return row;
}

SpaceListRow FolderRow(const gfx::Rect& bounds,
                       int id,
                       bool collapsed,
                       size_t items,
                       size_t end) {
  SpaceListRow row;
  row.kind = SpaceListRow::Kind::kFolder;
  row.bounds = bounds;
  row.pinned = true;
  row.folder_id = id;
  row.collapsed = collapsed;
  row.items = items;
  row.end = end;
  return row;
}

std::vector<SpaceListRow> ListRows() {
  std::vector<SpaceListRow> rows;
  for (size_t i = 0; i < SpaceRows().size(); ++i) {
    rows.push_back(TabRow(SpaceRows()[i], /*pinned=*/i < 2));
  }
  return rows;
}

SpaceListLayout ListLayout() {
  return {.header = gfx::Rect(0, -44, 200, 44),
          .separator = kSeparator,
          .normal_start = kNormalStart};
}

}  // namespace

TEST(KoanSpaceDragLogicTest, ADropGoesBeforeOrAfterTheRowByItsHalves) {
  EXPECT_TRUE(DropsBefore(gfx::Rect(0, 38, 200, 36), 38));
  EXPECT_TRUE(DropsBefore(gfx::Rect(0, 38, 200, 36), 55));
  EXPECT_FALSE(DropsBefore(gfx::Rect(0, 38, 200, 36), 56));
  EXPECT_FALSE(DropsBefore(gfx::Rect(0, 38, 200, 36), 73));
}

TEST(KoanSpaceDragLogicTest, ADropTakesTheSectionUnderThePointer) {
  // The first pinned row's top half: before it.
  SpaceListDrop drop = PlaceInSpaceList(ListRows(), ListLayout(), 5);
  EXPECT_TRUE(drop.pinned);
  EXPECT_EQ(0u, drop.slot);
  EXPECT_EQ(0, drop.line_y);
  // The second pinned row's lower half: after it, still pinned.
  drop = PlaceInSpaceList(ListRows(), ListLayout(), 70);
  EXPECT_TRUE(drop.pinned);
  EXPECT_EQ(2u, drop.slot);
  EXPECT_EQ(74, drop.line_y);
  // In the gap between two rows: as over the nearer one.
  drop = PlaceInSpaceList(ListRows(), ListLayout(), 37);
  EXPECT_EQ(1u, drop.slot);
  EXPECT_EQ(38, drop.line_y);
  // The separator: the end of the pinned section.
  drop = PlaceInSpaceList(ListRows(), ListLayout(), 90);
  EXPECT_TRUE(drop.pinned);
  EXPECT_EQ(2u, drop.slot);
  EXPECT_EQ(74, drop.line_y);
  // The New Tab row: the top of the normal tabs.
  drop = PlaceInSpaceList(ListRows(), ListLayout(), 100);
  EXPECT_FALSE(drop.pinned);
  EXPECT_EQ(2u, drop.slot);
  EXPECT_EQ(136, drop.line_y);
  // The last row's lower half, and the empty space below: last.
  drop = PlaceInSpaceList(ListRows(), ListLayout(), 200);
  EXPECT_FALSE(drop.pinned);
  EXPECT_EQ(4u, drop.slot);
  EXPECT_EQ(210, drop.line_y);
  drop = PlaceInSpaceList(ListRows(), ListLayout(), 500);
  EXPECT_EQ(4u, drop.slot);
  EXPECT_EQ(210, drop.line_y);
  EXPECT_EQ(SpaceRows()[3], drop.line_row);
}

TEST(KoanSpaceDragLogicTest, TheSpaceHeaderTakesADropFirstInThePinnedTabs) {
  const SpaceListDrop drop = PlaceInSpaceList(ListRows(), ListLayout(), -20);
  EXPECT_TRUE(drop.pinned);
  EXPECT_TRUE(drop.lit_header);
  EXPECT_EQ(0u, drop.slot);
  EXPECT_EQ(0, drop.folder);
}

TEST(KoanSpaceDragLogicTest, AnEmptySectionTakesADropAtItsEdge) {
  // No pinned tabs: the separator and the New Tab row at the top (0-22,
  // 22-58), then the normal rows.
  const std::vector<SpaceListRow> rows = {TabRow({0, 60, 200, 36}, false),
                                          TabRow({0, 98, 200, 36}, false)};
  const SpaceListLayout layout = {.separator = gfx::Rect(0, 0, 200, 22),
                                  .normal_start = 58};
  SpaceListDrop drop = PlaceInSpaceList(rows, layout, 10);
  EXPECT_TRUE(drop.pinned);
  EXPECT_EQ(0u, drop.slot);
  EXPECT_EQ(0, drop.line_y);
  drop = PlaceInSpaceList(rows, layout, 30);
  EXPECT_FALSE(drop.pinned);
  EXPECT_EQ(0u, drop.slot);
  EXPECT_EQ(60, drop.line_y);
  // No normal tabs: below the New Tab row.
  drop = PlaceInSpaceList(
      {TabRow({0, 0, 200, 36}, true)},
      {.separator = gfx::Rect(0, 38, 200, 22), .normal_start = 96}, 300);
  EXPECT_FALSE(drop.pinned);
  EXPECT_EQ(1u, drop.slot);
  EXPECT_EQ(96, drop.line_y);
}

TEST(KoanSpaceDragLogicTest, FolderRowsHaveZensZones) {
  EXPECT_EQ(FolderZone::kBefore, FolderRowZone(0.1f, false, 3));
  EXPECT_EQ(FolderZone::kInto, FolderRowZone(0.2f, false, 3));
  EXPECT_EQ(FolderZone::kInto, FolderRowZone(0.5f, false, 3));
  // An open folder with two items or more: its bottom is into it.
  EXPECT_EQ(FolderZone::kInto, FolderRowZone(0.9f, false, 2));
  // Collapsed, or with fewer than two: below it.
  EXPECT_EQ(FolderZone::kAfter, FolderRowZone(0.9f, true, 5));
  EXPECT_EQ(FolderZone::kAfter, FolderRowZone(0.9f, false, 1));
  EXPECT_EQ(FolderZone::kInto, FolderRowZone(0.8f, true, 5));
}

TEST(KoanSpaceDragLogicTest, DropsOnAFolderGoIntoItOrBesideIt) {
  // A pinned tab (0-36), folder 7 (38-74) open with a tab (76-112) and a
  // collapsed subfolder 8 (114-150), then the separator (152-174).
  std::vector<SpaceListRow> list = {
      TabRow({0, 0, 200, 36}, true),
      FolderRow({0, 38, 200, 36}, 7, false, 2, 4),
      TabRow({0, 76, 200, 36}, true, 7),
      FolderRow({0, 114, 200, 36}, 8, true, 3, 4),
  };
  list[3].folder = 7;
  const SpaceListLayout layout = {.separator = gfx::Rect(0, 152, 200, 22),
                                  .normal_start = 210};
  // Folder 7's top fifth: above it, outside it.
  SpaceListDrop drop = PlaceInSpaceList(list, layout, 40);
  EXPECT_EQ(1u, drop.slot);
  EXPECT_EQ(0, drop.folder);
  EXPECT_EQ(38, drop.line_y);
  EXPECT_FALSE(drop.lit_row);
  // Its middle: into it, first, its row lit.
  drop = PlaceInSpaceList(list, layout, 56);
  EXPECT_EQ(2u, drop.slot);
  EXPECT_EQ(7, drop.folder);
  EXPECT_EQ(1u, drop.lit_row);
  // Its tab's bottom half: after it, in folder 7.
  drop = PlaceInSpaceList(list, layout, 105);
  EXPECT_EQ(3u, drop.slot);
  EXPECT_EQ(7, drop.folder);
  EXPECT_EQ(112, drop.line_y);
  // The collapsed subfolder's bottom fifth: below it, in folder 7.
  drop = PlaceInSpaceList(list, layout, 145);
  EXPECT_EQ(4u, drop.slot);
  EXPECT_EQ(7, drop.folder);
  EXPECT_EQ(150, drop.line_y);
  // The separator: the end of the pinned section, out of the folders.
  drop = PlaceInSpaceList(list, layout, 160);
  EXPECT_TRUE(drop.pinned);
  EXPECT_EQ(4u, drop.slot);
  EXPECT_EQ(0, drop.folder);
}

namespace {

// Folder 7 (0-36) open with a tab (38-74) and its collapsed subfolder 8
// (76-112), folder 9 (114-150, collapsed), a pinned tab outside folders
// (152-188), the separator (190-212), a normal tab (240-276).
std::vector<SpaceListRow> FolderList() {
  std::vector<SpaceListRow> list = {
      FolderRow({0, 0, 200, 36}, 7, false, 2, 3),
      TabRow({0, 38, 200, 36}, true, 7),
      FolderRow({0, 76, 200, 36}, 8, true, 0, 3),
      FolderRow({0, 114, 200, 36}, 9, true, 0, 4),
      TabRow({0, 152, 200, 36}, true),
      TabRow({0, 240, 200, 36}, false),
  };
  list[2].folder = 7;
  return list;
}

SpaceListLayout FolderLayout() {
  return {.header = gfx::Rect(0, -44, 200, 44),
          .separator = gfx::Rect(0, 190, 200, 22),
          .normal_start = 230};
}

std::optional<FolderDrop> PlaceFolder(int id, int y, int refused = -1) {
  const std::vector<SpaceListRow> rows = FolderList();
  return PlaceFolderInSpaceList(
      rows, FolderLayout(), PlaceInSpaceList(rows, FolderLayout(), y), id,
      [&](int parent) { return parent != id && parent != refused; });
}

}  // namespace

TEST(KoanSpaceDragLogicTest, AFolderDropsAmongItsNewParentsSubfolders) {
  // Above folder 7: first at the top level.
  std::optional<FolderDrop> drop = PlaceFolder(9, 2);
  ASSERT_TRUE(drop);
  EXPECT_EQ(0, drop->parent);
  EXPECT_EQ(7, drop->before);
  EXPECT_EQ(0, drop->line_y);
  // The Space header: the same.
  drop = PlaceFolder(9, -20);
  ASSERT_TRUE(drop);
  EXPECT_EQ(0, drop->parent);
  EXPECT_EQ(7, drop->before);
  // Into folder 7, or after its tab: before its subfolder 8 (a folder's
  // tabs come first), the line on 8's top.
  for (int y : {18, 70}) {
    drop = PlaceFolder(9, y);
    ASSERT_TRUE(drop);
    EXPECT_EQ(7, drop->parent);
    EXPECT_EQ(8, drop->before);
    EXPECT_EQ(76, drop->line_y);
  }
  // Among the pinned tabs outside folders: after the last folder, the line
  // on the first such tab.
  drop = PlaceFolder(7, 160);
  ASSERT_TRUE(drop);
  EXPECT_EQ(0, drop->parent);
  EXPECT_EQ(0, drop->before);
  EXPECT_EQ(152, drop->line_y);
}

TEST(KoanSpaceDragLogicTest, AFolderDroppedOnItsOwnPlaceStaysThere) {
  // Right above itself: before the folder after it, the line where it is.
  const std::optional<FolderDrop> drop = PlaceFolder(7, 2);
  ASSERT_TRUE(drop);
  EXPECT_EQ(0, drop->parent);
  EXPECT_EQ(9, drop->before);
  EXPECT_EQ(0, drop->line_y);
}

TEST(KoanSpaceDragLogicTest, AFolderNeverDropsAmongNormalTabsOrInARefusal) {
  EXPECT_FALSE(PlaceFolder(9, 250));
  // Into one of its own subfolders (the controller refuses it).
  EXPECT_FALSE(PlaceFolder(7, 94, /*refused=*/8));
  // Into itself.
  EXPECT_FALSE(PlaceFolder(7, 18));
}

TEST(KoanSpaceDragLogicTest, TheDropLineHasZensInsetsAndRing) {
  // A row 200 wide at x 10: the line from 20 to 208, the ring before it.
  const gfx::Rect bounds = DropLineBounds(gfx::Rect(10, 40, 200, 36), 76);
  EXPECT_EQ(gfx::Rect(14, 73, 194, 8), bounds);
  // The line itself: its last 188px, 3px down.
  EXPECT_EQ(20, bounds.x() + kDropLineRingRoom);
  EXPECT_EQ(76, bounds.y() + kDropLineTop);
}

TEST(KoanSpaceDragLogicTest, TheDragImageLabelIsCutLikeZens) {
  EXPECT_EQ(u"Hello", TruncateDragLabel(u"Hello", 120));
  EXPECT_EQ(u"Hello wor", TruncateDragLabel(u"Hello world", 59));
  EXPECT_EQ(u"", TruncateDragLabel(u"Hello", 5));
  // Never half a surrogate pair.
  EXPECT_EQ(u"a", TruncateDragLabel(u"a\U0001F600b", 12));
}

TEST(KoanSpaceDragLogicTest, SeveralTabsAreHeldAtZensPoint) {
  EXPECT_EQ(gfx::Vector2d(40, 12), DragImageGrab(1, gfx::Vector2d(40, 12)));
  EXPECT_EQ(gfx::Vector2d(18, 18), DragImageGrab(3, gfx::Vector2d(40, 12)));
}

TEST(KoanSpaceDragLogicTest, ASplitShowsOverTheMiddleBandOfARow) {
  const gfx::Rect row(0, 100, 200, 40);
  // 40% to 60% of the height: 116 to 124.
  EXPECT_FALSE(SplitBandSide(row, {50, 115}, false));
  EXPECT_EQ(true, SplitBandSide(row, {50, 116}, false));
  EXPECT_EQ(true, SplitBandSide(row, {99, 120}, false));
  EXPECT_EQ(false, SplitBandSide(row, {100, 120}, false));
  EXPECT_EQ(false, SplitBandSide(row, {150, 124}, false));
  EXPECT_FALSE(SplitBandSide(row, {150, 125}, false));
  // Mirrored: the right half is the start one.
  EXPECT_EQ(true, SplitBandSide(row, {150, 120}, true));
  EXPECT_FALSE(SplitBandSide(row, {250, 120}, false));
}

TEST(KoanSpaceDragLogicTest, TheFakeTabTakesHalfTheRow) {
  const gfx::Rect row(10, 100, 200, 40);
  EXPECT_EQ(gfx::Rect(12, 102, 98, 36), SplitFakeTabBounds(row, true, false));
  EXPECT_EQ(gfx::Rect(110, 102, 98, 36), SplitFakeTabBounds(row, false, false));
  EXPECT_EQ(gfx::Rect(110, 102, 98, 36), SplitFakeTabBounds(row, true, true));
}

TEST(KoanSpaceDragLogicTest, PageEdgesSplitWithinAQuarter) {
  const gfx::Rect panels(100, 0, 800, 600);
  // The outer 10px and the middle: nothing.
  EXPECT_FALSE(PageSplitEdge(panels, {105, 300}));
  EXPECT_FALSE(PageSplitEdge(panels, {500, 300}));
  EXPECT_FALSE(PageSplitEdge(panels, {500, 595}));
  EXPECT_EQ(SplitEdge::kLeft, PageSplitEdge(panels, {150, 300}));
  EXPECT_EQ(SplitEdge::kRight, PageSplitEdge(panels, {850, 300}));
  EXPECT_EQ(SplitEdge::kTop, PageSplitEdge(panels, {500, 100}));
  EXPECT_EQ(SplitEdge::kBottom, PageSplitEdge(panels, {500, 500}));
  // In a corner, the nearer edge.
  EXPECT_EQ(SplitEdge::kTop, PageSplitEdge(panels, {200, 40}));
  EXPECT_EQ(SplitEdge::kLeft, PageSplitEdge(panels, {130, 100}));
}

TEST(KoanSpaceDragLogicTest, TheFakePaneTakesItsShare) {
  const gfx::Rect panels(100, 0, 800, 600);
  EXPECT_EQ(gfx::Rect(100, 0, 392, 600),
            FakePaneBounds(panels, SplitEdge::kLeft, 2, 8));
  EXPECT_EQ(gfx::Rect(508, 0, 392, 600),
            FakePaneBounds(panels, SplitEdge::kRight, 2, 8));
  EXPECT_EQ(gfx::Rect(100, 308, 800, 292),
            FakePaneBounds(panels, SplitEdge::kBottom, 2, 8));
}

TEST(KoanSpaceDragLogicTest, OverTheEssentialsTheImageIsATile) {
  EXPECT_EQ(gfx::Size(52, 48),
            EssentialDragTileSize(gfx::Size(52, 48), 230, 36));
  // No tile yet: 23.7% of the grid, at least the row's height and 4.
  EXPECT_EQ(gfx::Size(54, 36), EssentialDragTileSize(std::nullopt, 230, 36));
  EXPECT_EQ(gfx::Size(40, 36), EssentialDragTileSize(std::nullopt, 100, 36));
  // Grabbed at (30, 18): the tile 28 px right and 32 down of the pointer.
  EXPECT_EQ(gfx::Vector2d(-28, -32), EssentialTileGrab(gfx::Vector2d(30, 18)));
}

}  // namespace koan

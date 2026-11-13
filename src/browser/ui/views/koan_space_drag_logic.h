// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_LOGIC_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_LOGIC_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"

namespace koan {

// Zen's drag and drop between Spaces (ZenDragAndDrop.js, ZenSpaceIcons.mjs)
// and its Space switcher's layout, as plain logic for the unit tests.

// zen.workspaces.dnd-switch-padding: how near (either side) the sidebar's
// left or right edge a dragged tab switches Space.
inline constexpr int kSpaceDragSwitchPadding = 20;
// zen.tabs.dnd-switch-space-delay: how long it has to stay there.
inline constexpr base::TimeDelta kSpaceDragSwitchDelay =
    base::Milliseconds(500);
// ZenSpaceIcons.mjs: how far a Space button moves before it is reordered.
inline constexpr int kSpaceReorderThreshold = 5;

// Which way a tab dragged to `x` (in the sidebar's coordinates, LTR) switches
// Space in a sidebar `width` wide: -1 (the previous Space, near the left
// edge), 1 (the next, near the right edge) or 0.
int SpaceDragEdge(int x, int width);

// One Space button's extent along the bar (x in a row, y in a column).
struct SpaceButtonSpan {
  int start = 0;
  int end = 0;
};

// Zen's reorder: where the dragged button (`dragged`, an index into `spans`,
// which are in order) goes with the pointer at `pointer`. Over another
// button's first half it goes before it, over its second half after it;
// elsewhere it stays. Returns the dragged button's new index.
size_t ReorderedSpaceIndex(const std::vector<SpaceButtonSpan>& spans,
                           size_t dragged,
                           int pointer);

// Zen's Space switcher in the sidebar's bottom bar (#zen-workspaces-button,
// ZenSpaceManager.mjs onWindowResize(), zen-workspaces.css): `count` buttons
// in a strip `room` wide, each as wide as fits between `min_width` (the
// icons-overflow minimum) and `max_width`. They are centred while they fit;
// beyond that the strip scrolls sideways (overflow-x: auto, no scrollbar)
// and they start at its start (justify-content: safe center).
struct SpaceStripLayout {
  int button_width = 0;
  // Where the first button starts in the strip, before scrolling.
  int start = 0;
  // How far the strip scrolls: 0 while the buttons fit.
  int max_scroll = 0;
};
SpaceStripLayout LayoutSpaceStrip(int room,
                                  int count,
                                  int min_width,
                                  int max_width);

// The scroll-margin of a Space button (zen-workspaces.css).
inline constexpr int kSpaceStripScrollMargin = 20;

// Zen's scrollIntoView({inline: "nearest"}) on a Space button
// (ZenSpaceIcons.mjs, for the current Space and the hovered one): the
// strip's scroll offset, from `scroll`, that shows the button spanning
// [start, end) (unscrolled, in the strip) with its scroll-margin, moving as
// little as it can, within [0, max_scroll].
int ScrollSpaceIntoView(int scroll,
                        int room,
                        int max_scroll,
                        int start,
                        int end);

// How long Firefox's smooth scroll (scroll-behavior: smooth, which Zen's
// strip and scrollIntoView() use) takes to settle, and how far along it is
// after `elapsed` (0 to 1): a critically damped spring
// (layout.css.scroll-behavior.spring-constant 250, damping-ratio 1).
inline constexpr base::TimeDelta kSpaceStripScrollDuration =
    base::Milliseconds(600);
double SpaceStripScrollProgress(base::TimeDelta elapsed);

// Zen's drop of tabs on the Essentials grid
// (ZenPinnedTabManager.moveToAnotherTabContainerIfNecessary()): the tile
// under `point` takes them before it, or after it once `point` is past its
// horizontal middle (the far half: its left half in RTL); anywhere else in
// the grid they go after the last tile. Above the first row (the sidebar
// above the grid, EssentialsTopDropZone()) they go before the first tile, as
// above a Space's first row (PlaceInSpaceList()). `tiles` are the tiles'
// bounds in grid order (a split view is one tile). Returns the slot, 0 to
// tiles.size().
size_t EssentialsDropSlot(const std::vector<gfx::Rect>& tiles,
                          const gfx::Point& point,
                          bool rtl);

// With no Essentials, the part of the sidebar a dragged tab is over them in
// (the promo shows, lit, and a drop there makes the tab an Essential), in the
// coordinates of the sidebar of `sidebar` size: its full width, from its top
// down to the Space header's vertical middle (`header`) until the promo
// shows, then down to the promo's bottom (`promo`, once laid out). Zen's
// empty grid is only a 2px strip above the Space header
// (.zen-essentials-container's min-height) and its URL bar is not part of
// the tab strip; Koan takes the whole top of the sidebar, so that a tab
// dragged straight up, past the strip and over the URL bar, still gets there.
gfx::Rect EmptyEssentialsDropZone(const gfx::Size& sidebar,
                                  const gfx::Rect& header,
                                  const std::optional<gfx::Rect>& promo);

// With Essentials, the part of the sidebar above the grid (`grid`, in the
// sidebar's coordinates) that also counts as over them: its full width, from
// its top down to the grid's (the window buttons' row and the URL bar). A
// drop there goes before the first tile (EssentialsDropSlot()). Empty while
// the grid is (no Essentials: EmptyEssentialsDropZone() instead).
gfx::Rect EssentialsTopDropZone(const gfx::Size& sidebar,
                                const gfx::Rect& grid);

// Zen's two sections of a Space's list (ZenSpace.mjs): the pinned section
// (.zen-workspace-pinned-tabs-section: the pinned tabs and folders, then the
// separator with Clear) and the normal one (.zen-workspace-normal-tabs-section:
// the "+ New Tab" row, then the normal tabs, then the empty space below).
// A drop decides by the section under the pointer, not by the tabs around
// the drop (ZenPinnedTabManager's moveToAnotherTabContainerIfNecessary()),
// the Space header (.zen-current-workspace-indicator) counting as pinned.
// The pinned section ends at the separator's bottom, `pinned_end` (the top
// of the New Tab row).
inline bool IsInPinnedDropSection(int y, int pinned_end) {
  return y < pinned_end;
}

// One row of a Space's list as Koan's drag places a drop
// (ZenDragAndDrop.js #applyDragoverIndicator()): a tab's row (a split view's
// two tabs as one) or a folder's row (its label), in the sidebar's
// coordinates.
struct SpaceListRow {
  enum class Kind { kTab, kFolder };
  Kind kind = Kind::kTab;
  gfx::Rect bounds;
  // In the Space's pinned section (a folder's row always is).
  bool pinned = false;
  // A tab: the folder holding it; a folder: the folder it is in (0: none).
  int folder = 0;
  // A folder's row: the folder, whether it is collapsed, how many items
  // (tabs and subfolders) it has, and the index of the first row after the
  // ones it shows (its subtree).
  int folder_id = 0;
  bool collapsed = false;
  size_t items = 0;
  size_t end = 0;
};

// The Space's list around its rows, in the sidebar's coordinates: the Space
// header (empty if none), the separator's row (its top is where the line of
// an empty pinned section goes; its bottom ends the pinned section,
// IsInPinnedDropSection()) and the bottom of the New Tab row (where the line
// of an empty normal section goes).
struct SpaceListLayout {
  gfx::Rect header;
  gfx::Rect separator;
  int normal_start = 0;
};

// zen.tabs.folder-dragover-threshold-percent: a folder's row is split into
// its top fifth, its bottom fifth and the middle.
inline constexpr float kFolderDragoverThreshold = 0.2f;

// Where over a folder's row (`fraction` of its height from its top) a drop
// goes: above the folder (its top fifth), below it (its bottom fifth, while
// it is collapsed or has fewer than two items) or into it (the rest), Zen's
// zones.
enum class FolderZone { kBefore, kInto, kAfter };
FolderZone FolderRowZone(float fraction, bool collapsed, size_t items);

// Zen's moveOverThresholdPercent (50): over a row's top half a drop goes
// before it, over its bottom half after it.
bool DropsBefore(const gfx::Rect& row, int y);

// Where tabs dropped at `y` on a Space's list go, Zen's rules
// (#applyDragoverIndicator(), moveToAnotherTabContainerIfNecessary()):
// - over the Space header: first in the pinned section, the header lit;
// - pinned or not by the section under the pointer (IsInPinnedDropSection());
// - over a tab's row, before it or after it by its halves (DropsBefore()), in
//   the folder holding it; between rows as over the nearest one;
// - over a folder's row by its zones (FolderRowZone()): above or below the
//   folder, in the folder holding it, or into it (first), the row lit;
// - past a section's last row (the separator, the empty space below the
//   tabs), at the end of the section outside folders, and in an empty section
//   at the separator's top or below the New Tab row.
// `rows` are top to bottom, the pinned section's first.
struct SpaceListDrop {
  bool pinned = false;
  // Before rows[slot]; rows.size(): after the last.
  size_t slot = 0;
  // The folder the tabs go in (0: none).
  int folder = 0;
  // A drop into a folder lights its row, a drop on the Space header the
  // header: no line then.
  std::optional<size_t> lit_row;
  bool lit_header = false;
  // Otherwise the line goes across `line_row` at `line_y`.
  gfx::Rect line_row;
  int line_y = 0;
};
SpaceListDrop PlaceInSpaceList(const std::vector<SpaceListRow>& rows,
                               const SpaceListLayout& layout,
                               int y);

// Zen's #zen-drag-indicator across a row (vertical-tabs.css): a 2px line from
// 10px in from the row's start to 2px before its end, its top at `y`, with a
// hollow ring (2px wide, 8px across) centred 2px before the line's start.
// Returns the indicator's bounds, the ring included: the line is its last
// `width - kDropLineRingRoom` px at kDropLineTop.
inline constexpr int kDropLineThickness = 2;
inline constexpr int kDropLineRingSize = 8;
inline constexpr int kDropLineRingRoom = 6;
inline constexpr int kDropLineTop = 3;
gfx::Rect DropLineBounds(const gfx::Rect& row, int y);

// Zen's drag image (#appendDragImageClone(), #getDragImageOffset()): up to
// three rows, each 4px right of and below the one before at 20% opacity
// behind it; the label cut to a character per 6px of its width; the pointer
// where the tab was grabbed (`grab`, in its row), or at (18, 18) with several
// tabs; their count on a 16px dot off the top-left corner.
inline constexpr size_t kDragImageMaxRows = 3;
inline constexpr int kDragImageRowOffset = 4;
inline constexpr float kDragImageBackRowOpacity = 0.2f;
inline constexpr int kDragImageCountDotSize = 16;
inline constexpr int kDragImageCountDotLeft = -16;
inline constexpr int kDragImageCountDotTop = -10;
inline constexpr int kDragImageCountFontSize = 10;
std::u16string TruncateDragLabel(std::u16string_view title, int label_width);
gfx::Vector2d DragImageGrab(size_t tab_count, const gfx::Vector2d& grab);

// Zen's split by drag (ZenDragAndDrop.js _handle_tabDragOverToSplit(),
// ZenViewSplitter.mjs onBrowserDragOverToSplit()).
//
// zen.splitView.drag-over-split-threshold (40) and
// zen.splitView.drag-over-split-delayMC (500): a tab held over the middle of
// another tab's row (from 40% to 60% of its height) for half a second shows
// the split there.
inline constexpr float kSplitDragThreshold = 0.4f;
inline constexpr base::TimeDelta kSplitDragDelay = base::Milliseconds(500);
// Whether `point` is in that middle band of `row`, and on which half of it
// (true: the start half, where the dragged tab goes first). nullopt outside
// the band.
std::optional<bool> SplitBandSide(const gfx::Rect& row,
                                  const gfx::Point& point,
                                  bool rtl);
// <zen-split-fake-tab> in the row (zen-split-view.css): its start or end half
// less the tab's block margin.
inline constexpr int kSplitFakeTabMargin = 2;
gfx::Rect SplitFakeTabBounds(const gfx::Rect& row, bool start, bool rtl);

// The page's edges: within a quarter of the panels' width (height) from one,
// but not its outer 10px; the nearest edge wins.
enum class SplitEdge { kLeft, kRight, kTop, kBottom };
inline constexpr int kSplitEdgeIgnored = 10;
std::optional<SplitEdge> PageSplitEdge(const gfx::Rect& panels,
                                       const gfx::Point& point);
// #zen-split-view-fake-browser once open: its share of the panels on `edge`,
// 1 / `panes` of them (the panes after the split), less the element
// separation `gap`.
gfx::Rect FakePaneBounds(const gfx::Rect& panels,
                         SplitEdge edge,
                         int panes,
                         int gap);
// Zen's page-edge drag image (#zen-split-view-drag-image): a 200x250 card,
// 16px corners, 20px padding, the 24px favicon above the 14px bold title,
// 20px apart, centred.
inline constexpr gfx::Size kSplitDragCardSize(200, 250);
inline constexpr int kSplitDragCardRadius = 16;
inline constexpr int kSplitDragCardPadding = 20;
inline constexpr int kSplitDragCardIconSize = 24;
inline constexpr int kSplitDragCardGap = 20;
inline constexpr int kSplitDragCardFontSize = 14;
// The fake pane opens (and closes) in 100 ms, easing out.
inline constexpr base::TimeDelta kSplitFakePaneDuration =
    base::Milliseconds(100);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACE_DRAG_LOGIC_H_

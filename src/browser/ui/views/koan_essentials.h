// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_H_

class TabStripView;

namespace gfx {
class Canvas;
}  // namespace gfx

namespace tabs {
enum class VerticalTabStripCollapseState;
}  // namespace tabs

namespace views {
class SizeBounds;
class View;
}  // namespace views

namespace koan {

// Hooks for the vertical tab strip's pinned tabs, which Koan shows as Zen's
// Essentials: a grid of square tiles at the top of the sidebar
// (pinned_tab_container_view.cc, vertical_tab_style_views.cc).

// Caps the number of tiles per row (`columns` fit the width) at Zen's four.
int ClampEssentialsColumns(int columns);

// A tab that becomes an Essential leaves its Space's list and appears in the
// grid as one transition, as in Zen (its grid's `max-height 0.3s ease-out`):
// upstream fades the row out and the tile in with one shared animation, and
// these hooks make the grid's height follow it, so that the Space below
// slides instead of jumping a row in the first frame.
//
// The height the vertical strip's layout gives the Essentials grid
// (tab_strip_view_layout.cc), given its final height `target_height`: while
// the strip's shared tab animation (TabCollectionAnimatingLayoutManager's
// AnimationCoordinator) runs, the height eases from the one shown when the
// target last changed to the target, in step with the tiles and rows;
// otherwise the target. Upstream's own interpolated height (`animate_host_size`)
// counts a new tile's full height from the first frame (a tile grows in width
// only), so it jumps a row; this one doesn't.
int GetEssentialsHeight(const TabStripView* tab_strip_view, int target_height);

// The width of the empty grid (pinned_tab_container_view_vertical_layout.cc):
// the width it is laid out at, as with tiles (upstream's is 0). Upstream
// snaps, without animating, any layout whose width changes, so the first
// Essential would appear at once.
int GetEmptyEssentialsWidth(const views::SizeBounds& size_bounds);

// The pinned container's minimum height in the vertical strip's layout
// (tab_strip_view_layout.cc), given upstream's (`min_height`, a row once there
// is a tile) and its `preferred_height` (GetEssentialsHeight(), while it
// grows): at most the latter, so that a first row grows in with the tile. At
// rest the preferred height is never below the minimum, so this changes
// nothing then.
int GetEssentialsMinHeight(int min_height, int preferred_height);

// The strip's collapse state as the Essentials grid sees it
// (pinned_tab_container_view*.cc): upstream lays out one tile per row, as
// rows with titles, while the strip is collapsed. Koan's strip is "collapsed"
// only while compact mode hides the sidebar (koan::OwnsSidebar(): no icons-only
// rail), and its revealed sidebar keeps Zen's grid of tiles
// (compact-mode/sidebar.inc.css keeps `.zen-essentials-container`), so with
// Koan's sidebar the grid is always laid out as expanded.
tabs::VerticalTabStripCollapseState GetEssentialsCollapseState(
    tabs::VerticalTabStripCollapseState state);

// Whether a pinned tab's title shows (tab_view_vertical_layout.cc), given
// upstream's `shows` (in the collapsed strip's expand-on-hover). Essentials
// tiles never show one with Koan's sidebar.
bool ShowsPinnedTabTitle(bool shows);

// Paints the background of the pinned tab `tab` as an Essentials tile
// (`selected`: active or multi-selected; `hover`: the hover animation's
// progress, 0..1). Called by PaintSidebarTab (koan_sidebar_tabs.h) with
// Koan's sidebar on.
void PaintEssentialsTile(gfx::Canvas* canvas,
                         const views::View& tab,
                         bool selected,
                         double hover,
                         float corner_radius);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ESSENTIALS_H_

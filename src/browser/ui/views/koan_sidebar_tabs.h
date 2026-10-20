// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_TABS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_TABS_H_

class TabStripView;

namespace gfx {
class Canvas;
}  // namespace gfx

namespace views {
class View;
struct ProposedLayout;
}  // namespace views

namespace koan {

// Hooks for TabStripView's vertical layout (tab_strip_view_layout.cc). Koan
// puts Zen's rows between the pinned and unpinned tabs: the separator with a
// "Clear" button (closes all unpinned tabs) and the "+ New Tab" row. The rows
// are installed by KoanSidebarTabsController; without them these lay out
// nothing.

// The rows' height, 0 when they are not installed.
int GetSidebarTabRowsHeight(const TabStripView* tab_strip_view);

// Appends the rows to `layout` at (0, `y`), `width` wide, and returns their
// height.
int PlaceSidebarTabRows(const TabStripView* tab_strip_view,
                        views::ProposedLayout& layout,
                        int width,
                        int y);

// Whether `view` is Koan's rows view. Clicks on the rows must reach them, so
// they are never part of the window caption.
bool IsSidebarTabRowsView(const views::View* view);

// Hook for VerticalTabStyleViews::PaintTab: paints the background of `tab`,
// a vertical tab, as Zen's tab row (or Essentials tile when `pinned`) and
// returns true, or returns false (Koan's sidebar is off) to leave it to
// upstream. `selected` is true for the active and multi-selected tabs, `hover`
// is the hover animation's progress (0..1).
bool PaintSidebarTab(gfx::Canvas* canvas,
                     const views::View& tab,
                     bool pinned,
                     bool selected,
                     double hover,
                     float corner_radius);

// Hook for TabViewVerticalLayout: the gap between a tab's favicon and its
// title (Zen's 10px), or `upstream_value` with Koan's sidebar off.
int SidebarTabIconGap(int upstream_value);

// Hooks for VerticalTabStripRegionView's resizing: the expanded sidebar's
// width limits (Zen's 163 to 500px) and the width below which a drag
// collapses it, or upstream's `upstream_value` with Koan's sidebar off.
int SidebarMinWidth(int upstream_value);
int SidebarMaxWidth(int upstream_value);
int SidebarCollapseSnapWidth(int upstream_value);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SIDEBAR_TABS_H_

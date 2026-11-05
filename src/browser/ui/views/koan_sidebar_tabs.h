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
class SizeBounds;
class View;
struct ProposedLayout;
}  // namespace views

namespace koan {

// Zen's rows between a Space's pinned and normal tabs: the separator with a
// "Clear" button (closes all unpinned tabs) and the "+ New Tab" row. They
// are installed by KoanSidebarTabsController in the active Space's tab group
// view, inside the scrolling tab list (Zen's pinned section, the separator
// and the normal tabs scroll together), and laid out by PlaceSpaceTabRows().
// Without a Space (e.g. Incognito) they sit in the tab strip, above the tab
// list, laid out by the TabStripView hooks below. Without the rows these lay
// out nothing.

// Hooks for TabStripView's vertical layout (tab_strip_view_layout.cc).
// The rows' height in the tab strip, 0 when they are not there.
int GetSidebarTabRowsHeight(const TabStripView* tab_strip_view);

// Appends the rows to `layout` at (0, `y`), `width` wide, and returns their
// height, if they are in the tab strip.
int PlaceSidebarTabRows(const TabStripView* tab_strip_view,
                        views::ProposedLayout& layout,
                        int width,
                        int y);

// Hook for TabGroupViewVerticalLayout, called before laying out each child
// `next_child` of `tab_group_view` and once more with null after the last:
// if the rows are in this group and `next_child` doesn't belong to the
// Space's pinned tabs, appends them to `layout` and returns the height the
// caller moves down by (0 otherwise, and once they are placed). After pinned
// tabs they go at `y`, followed by `tab_padding`; without pinned tabs at the
// top of the group, in its top margin, as in the tab strip before. Tabs are
// inset by the strip's padding when `collapsed`, and so are the rows.
int PlaceSpaceTabRows(const views::View* tab_group_view,
                      const views::View* next_child,
                      bool collapsed,
                      int y,
                      const views::SizeBounds& size_bounds,
                      int tab_padding,
                      views::ProposedLayout& layout);

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

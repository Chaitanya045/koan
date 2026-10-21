// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACES_H_

#include <memory>

class TabStripModel;

namespace ui {
class SimpleMenuModel;
}

namespace views {
struct ProposedLayout;
class View;
}  // namespace views

namespace koan {

// Hooks for Koan's Spaces (KoanWorkspaceController), called from upstream
// code through patches.

// TabStripViewLayout (vertical): lays out the Space header, or the
// Create-a-Space form in its place, between the pinned tabs and the
// Space's tabs. `available_height` is 0 when the height is unbounded.
// Returns the height taken.
int PlaceSpaceHeader(const views::View* tab_strip_view,
                     bool collapsed,
                     int width,
                     int y,
                     int available_height,
                     views::ProposedLayout& layouts);

// For AddZenTabMenuItems() (koan_tab_menu.h): with Spaces on, replaces the
// tab group items of the tab context menu for the tab at `index` (focus, add
// to and remove from a group: a Space is a group) with Zen's "Move To"
// submenu, listing the other Spaces. Returns the submenu, which the caller
// keeps alive; null when there is none.
std::unique_ptr<ui::SimpleMenuModel> ReplaceTabGroupMenuItems(
    ui::SimpleMenuModel* menu,
    TabStripModel* tab_strip,
    int index);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACES_H_

// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACES_H_

class TabStripModel;

namespace tab_groups {
class TabGroupId;
}

namespace tabs {
class TabInterface;
}

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

// For AddKoanTabMenuItems() (koan_tab_menu_hooks.h): with Spaces on,
// replaces the tab group items of the tab context menu for the tab at
// `index` (focus, add to and remove from a group: a Space is a group) with
// Zen's "Move To" submenu, listing the other Spaces. The window's
// KoanWorkspaceController keeps the submenu.
void ReplaceTabGroupMenuItems(ui::SimpleMenuModel* menu,
                              TabStripModel* tab_strip,
                              int index);

// TabStripModel::EnterFocusMode(), when the active tab isn't in `group`:
// the tab to show, the Space's last active tab (Zen), instead of the
// group's first one. Every way of switching Space (Koan's, upstream's swipe
// and keys) goes through it, so the first tab never loads on the way. Null:
// upstream decides (no Spaces in the window).
tabs::TabInterface* PickSpaceTab(TabStripModel* tab_strip,
                                 const tab_groups::TabGroupId& group);

// TabStripModel::RotateFocusedGroup() (upstream's swipe): whether swiping
// past the last or first Space wraps around (Zen's
// zen.workspaces.wrap-around-navigation) instead of leaving focus mode.
bool WrapsSpaces(TabStripModel* tab_strip);

// TabGroupViewLayout: whether the focused group in `tab_group_view`'s
// window is a Space, whose header Koan's Space header replaces (with the
// "Unfocus" pill). Not in windows without Spaces (e.g. Incognito).
bool ShowsSpaceHeader(const views::View* tab_group_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACES_H_

// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACES_H_

#include <optional>

class BrowserView;
class BrowserWindowInterface;
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

namespace gfx {
class Point;
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

// VerticalTabStripRegionView::IsPositionInWindowCaption(): whether `point`
// (in `region`'s coordinates) is over the Create-a-Space form in
// `tab_strip_view`. The form is client area: on macOS a point that counts as
// window caption never reaches the views (AppKit starts a window drag), so
// its buttons would take no clicks.
bool IsInSpaceForm(const views::View* tab_strip_view,
                   const views::View* region,
                   const gfx::Point& point);
// Zen's empty tab: the blank page a window shows when its last tab closes. It
// is never listed (no sidebar row), never navigates (what the URL bar opens
// from it opens in a new tab) and Cmd+W on it closes the window. Zen keeps
// one per window; here each Space with nothing else has one, since a Space
// (a tab group) needs a tab to exist. Marked by Koan when it makes one (kept
// with the session), and an empty page still. Not for Essentials or when
// Spaces are off.
bool IsEmptyTab(const tabs::TabInterface* tab);

// TabGroupViewVerticalLayout: whether `view` is a tab row to hide (see
// IsEmptyTab()).
bool IsEmptyTabView(const views::View* view);

// Browser::TabStripEmpty(): Zen keeps the window when its last tab closes
// (browser.tabs.closeWindowWithLastTab is false). Returns true if `browser`
// stays: it gets an empty tab (a new tab page in the window's Space) soon.
// False, so the window closes, when the window itself is closing, in a
// private window, or without Koan's Spaces.
bool KeepWindowOnLastTabClosed(BrowserWindowInterface* browser);

// The Spaces feature of the tab context menu (koan_tab_menu.h): with Spaces on,
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

// Whether `tab_strip`'s window has Spaces (koan_tab_menu.cc swallows
// upstream's tab group commands there).
bool HasSpaces(TabStripModel* tab_strip);

// BrowserCommandController::HandleCommandWithDisposition(), first: runs
// upstream's tab group command `command_id` the way a Spaces window does
// (KoanWorkspaceController::RunGroupCommand()) and returns true, or returns
// false (another command, or no Spaces). Only reached without
// kUseActionsForBrowserCommands; with it the window's ActionItems do the same.
bool HandleSpacesCommand(BrowserWindowInterface* browser, int command_id);

// TabStripCollectionController::UpdateFocusModeTheme(): upstream gives a
// focused group's window the group's colour (a per-window user colour
// override, which also suppresses the profile's theme). In a Spaces window
// Koan sets the override instead (KoanWorkspaceController): the Space
// theme's colour, or none for Zen's default theme, so the user's colour and
// theme show. Returns false where upstream decides (no Spaces).
bool ApplySpaceColor(TabStripModel* tab_strip);

// TabView::UpdateFocusFreezing(): whether `tab`'s window has Spaces, whose
// other Spaces' tabs upstream's focus mode must not freeze (Koan's
// background-tab policy decides, docs/PERFORMANCE.md; Zen freezes nothing
// per Space).
bool KeepsOtherSpacesRunning(const tabs::TabInterface* tab);

// BrowserNativeWidgetMac::ValidateUserInterfaceItem() (the Mac menu bar),
// after upstream's own validation: Koan's one table of the menu bar items it
// hides per window. For an item Koan decides, whether it is hidden (and
// disabled) in `browser_view`'s window, or shown; nullopt for every other item
// (upstream's state stands). The menu bar is shared by the windows, so each
// window's validation sets an owned item both ways. Today:
// - View > "Show Tabs Vertically", hidden while Koan owns the sidebar (#154).
//   "Collapse Vertical Tabs" stays, as the menu item of Shift+Cmd+L: AppKit
//   doesn't fire a hidden item's key equivalent.
// - View > Always Show Toolbar in Full Screen, hidden while Koan's toolbar is
//   hidden.
// - The Tab menu's Group Tab and its Option alternate, hidden in a Spaces
//   window.
// Other features add their items here rather than set the hidden state
// themselves.
std::optional<bool> HidesMenuCommand(BrowserView* browser_view, int command_id);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACES_H_

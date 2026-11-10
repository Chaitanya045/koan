// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPACES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPACES_H_

#include <optional>
#include <string>

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

// The empty tab stops being one when it navigates in place (a bookmark, the
// Library, a dropped link, chrome.tabs.update): its mark goes, so that Back
// into the new tab page later is an ordinary tab with a row, and Cmd+W
// closes the tab, not the window. Called on every change of the tab.
void ClearEmptyTabMarkIfNavigated(tabs::TabInterface* tab);

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

// TabStripModel::AddWebContents(), for a tab added with no index that isn't
// a link's (Ctrl+T, the URL bar's new tab, chrome.tabs.create()): Zen puts it
// at the top of the Space (zen.view.show-newtab-button-top, Tabbrowser's
// #insertTabAtIndex), where the New Tab row is, not at the end of the window.
// Sets `index` and `group` to the start of the focused Space unless `group` is
// already set, the tab is pinned, or the window has no Spaces.
void PlaceUnindexedTab(TabStripModel* tab_strip,
                       bool pinned,
                       int& index,
                       std::optional<tab_groups::TabGroupId>& group);

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

// Agent containers (koan/browser/containers/koan_agent_containers.h): makes
// the ungrouped tab at `index` of `tab_strip` a new Space named `name` with
// the emoji `icon`, without showing it or changing the active tab. Nullopt if
// the window has no Spaces.
std::optional<tab_groups::TabGroupId> CreateBackgroundSpace(
    TabStripModel* tab_strip,
    int index,
    const std::u16string& name,
    const std::u16string& icon);

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

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPACES_H_

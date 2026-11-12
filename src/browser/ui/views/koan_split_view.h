// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_VIEW_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_VIEW_H_

class BrowserView;
class BrowserWindowInterface;
class TabStripModel;

namespace split_tabs {
enum class SplitTabCreatedSource;
enum class SplitTabLayout;
}  // namespace split_tabs

namespace ui {
class SimpleMenuModel;
}

namespace koan {

// Zen's split view item of the tab context menu (tab-zen-split-tabs), for
// the tab at `index` (`num_tabs` tabs selected for the command); split view's
// TabMenuFeature (koan_tab_menu.h): upstream's
// "Add tab to new split view" / "New split view with current tab" become
// "Add Split View..." (one tab) or "Join N Tabs" (a selection), and a tab in
// a split gets "Split Out Tab" ahead of upstream's "Arrange split view".
void AddZenSplitTabMenuItems(ui::SimpleMenuModel* menu,
                             TabStripModel* tab_strip,
                             int index,
                             int num_tabs);

// Command id of "Split Out Tab", in split view's range of tab menu commands
// (TabMenuFeatureId::kSplitView in koan_tab_menu.h).
inline constexpr int kSplitOutTabCommandId = 3001;

inline bool IsSplitOutTabCommand(int command_id) {
  return command_id == kSplitOutTabCommandId;
}

// "Split Out Tab" for the tab at `index`: takes it out of its split view.
// Chromium's split views have two tabs, so the split is separated.
bool CanSplitOutTab(const TabStripModel* tab_strip, int index);
void SplitOutTab(TabStripModel* tab_strip, int index);

// Zen's split-view limit toast ("Can't add more panels to the split
// view!"), for a third panel joining a split (Zen takes four, Chromium two).
// Koan-internal, not a hook.
void ShowSplitLimitToast(BrowserView* browser_view);

// Hook: chrome::NewSplitTab(), where every "new split" path ends
// (IDC_NEW_SPLIT_TAB, Cmd+Shift+*, the palette, the Space's create menu,
// "Add Split View..." on the active tab, the Mac menu): Zen's one-tab split
// (ZenViewSplitter createEmptySplit()) in place of upstream's split view tab
// picker page. The Space's empty tab (IsEmptyTab(), koan_spaces.h; a new
// blank tab in a window without Spaces) joins the active tab in a split, on
// its right (`layout` side by side) or below it (stacked), and is selected,
// and the URL bar opens over it as for a new tab. What is opened from it
// replaces the empty pane; the bar closed without opening anything takes the
// pane away and selects the tab again. Returns true when Koan handled the
// call: also when it did nothing, as Zen's command is disabled then (the
// active tab is the empty tab or in a split; an Essential, which Zen
// duplicates into a split instead, not done here). False leaves it to
// upstream (no Koan URL bar in this window).
bool OpenEmptySplit(BrowserWindowInterface* browser,
                    split_tabs::SplitTabLayout layout,
                    split_tabs::SplitTabCreatedSource source);

// Hook: MultiContentsViewMiniToolbar::UpdateState(). Zen's split panes have
// no mini toolbar (favicon, domain, menu and close over the pane's corner),
// so Koan keeps upstream's hidden, with no size: ContentsContainerOutline
// and ContentsCaptureBorderView cut a notch the toolbar's size into the
// outline. A pane closes and swaps from the tab menu ("Split Out Tab",
// "Arrange split view") and Cmd+Option+U.
bool HidesSplitMiniToolbar();

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_VIEW_H_

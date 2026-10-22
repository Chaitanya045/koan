// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_VIEW_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_VIEW_H_

class BrowserView;
class TabStripModel;

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

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SPLIT_VIEW_H_

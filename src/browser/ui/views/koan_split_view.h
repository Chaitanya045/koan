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

// Zen's split view item of the tab context menu (tab-zen-split-tabs), called
// by the hook in TabMenuModel::Build() after upstream's split view items for
// the tab at `index` (`num_tabs` tabs selected for the command): upstream's
// "Add tab to new split view" / "New split view with current tab" become
// "Add Split View..." (one tab) or "Join N Tabs" (a selection), and a tab in
// a split gets "Split Out Tab" ahead of upstream's "Arrange split view".
void AddZenSplitTabMenuItems(ui::SimpleMenuModel* menu,
                             TabStripModel* tab_strip,
                             int index,
                             int num_tabs);

// Command id of "Split Out Tab", clear of the tab context menu's commands
// (tabs::TabContextMenuCommand) and its submenus' ranges (1001-2100, see
// ExistingBaseSubMenuModel). Handled by the hooks in TabStripModel's
// IsContextMenuCommandEnabled() and ExecuteContextMenuCommand().
// TabContextMenuController casts every command id to
// tabs::TabContextMenuCommand (an unscoped enum of 0-30) before the hooks
// see it; casting 3001 is formally unspecified but harmless (no
// -fstrict-enums), and keeps the hooks out of the views controller.
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

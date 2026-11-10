// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_VERTICAL_TABS_POLICY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_VERTICAL_TABS_POLICY_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/prefs/pref_change_registrar.h"

class BrowserView;
class PrefService;

namespace tabs {
enum class VerticalTabStripCollapseState;
}  // namespace tabs

namespace actions {
class ActionItem;
}  // namespace actions

namespace views {
class View;
}  // namespace views

namespace koan {

// Koan owns the sidebar. The sidebar is upstream's vertical tab strip, and
// this is the one place in Koan that decides what that strip's state machine
// may do in one window (the audit and the decision are in
// .agent/notes/upstream/vertical-tabs.md, option A; the rules in
// koan_vertical_tabs.h are what the hook in VerticalTabStripStateControllerImpl
// enforces):
//
// - vertical tabs are always on, expand-on-hover is always on, and the strip
//   is collapsed only while compact mode hides the sidebar, which is never
//   saved (the hook);
// - every command, menu item, shortcut and setting that reaches the strip's
//   state either does what Koan does with it or nothing (the command
//   policy's kSidebar rows and InstallActions(),
//   RemoveOldPrefs(), the hooks that call ShowSidebarButtonContextMenu() and
//   koan::OnSidebarDragCollapsed());
// - the strip is collapsed and expanded by one function, SetSidebarHidden(),
//   which is also the one place that knows a window is being destroyed.
//
// In a debug build, a change to the strip's mode or collapse state that did
// not come from here is reported (DLOG(FATAL)), so that a new upstream entry
// point fails the tests instead of reaching users.
//
// One per window, owned by KoanBrowserViewController.
class KoanVerticalTabsPolicy {
 public:
  explicit KoanVerticalTabsPolicy(BrowserView* browser_view);
  KoanVerticalTabsPolicy(const KoanVerticalTabsPolicy&) = delete;
  KoanVerticalTabsPolicy& operator=(const KoanVerticalTabsPolicy&) = delete;
  ~KoanVerticalTabsPolicy();

  // Called by KoanBrowserViewController.
  void OnAddedToWidget();
  void OnBrowserViewDestroying();

  // Whether the window is closing or destroyed, the one answer for all of
  // Koan's per-window code (compact mode asks it too): the widget is closed,
  // its deletion is scheduled, or OnBrowserViewDestroying() ran. Then nothing
  // starts a sidebar animation (the browser's animation controller is gone or
  // about to be) and locks are let go of later.
  bool IsClosing() const;

  // Compact mode hides the sidebar (collapses the strip, which then slides
  // over the page on hover) or gives it back (expands it). Nothing else
  // collapses the strip; does nothing once the window is being destroyed (the
  // browser's animation controller is already gone then).
  void SetSidebarHidden(bool hidden);

  // The sidebar button's text and tooltip (also its menu item) say what it
  // does now: toggles compact mode. Upstream rewrites them ("Collapse tabs")
  // on its own schedule, so this runs again when compact mode changes.
  void UpdateSidebarButtonText();

  // The sidebar button's action (the command policy's kSidebar row): in Zen
  // the sidebar button toggles compact mode.
  void OnSidebarButtonAction();

 private:
  PrefService* GetPrefs() const;
  actions::ActionItem* FindAction(int action_id) const;
  // The state of the window's own actions that reach the strip (enabled,
  // the sidebar button's key and text). What they run is the command
  // policy's (koan_command_policy.cc, kSidebar rows): with
  // kUseActionsForBrowserCommands (forced on) every route to them, the menu
  // bar, shortcuts and chrome::ExecuteCommand(), runs those.
  void InstallActions();
  // Old profiles (or chrome://settings) may have the three prefs upstream
  // reads for the strip's mode and hover; the hook ignores them, and this
  // keeps them at their defaults so that the settings page is truthful.
  void RemoveOldPrefs();
  void StartWatching();
  void OnCollapseChanged(tabs::VerticalTabStripCollapseState state);
  void NotifySettled();

  const raw_ptr<BrowserView> browser_view_;
  PrefChangeRegistrar pref_change_registrar_;
  bool added_ = false;
  bool destroying_ = false;
  // A request to collapse or expand is under way (its animation hasn't ended).
  bool settling_ = false;
  // What SetSidebarHidden() last asked for (debug builds check that the strip
  // settles there).
  bool hidden_ = false;
  base::CallbackListSubscription mode_subscription_;
  base::CallbackListSubscription collapse_subscription_;

  base::WeakPtrFactory<KoanVerticalTabsPolicy> weak_factory_{this};
};

// The vertical tab strip's content column in `browser_view` (upstream's
// content area: the tab strip, then its bottom container, which flexes to
// fill the room, so views appended after it sit at the bottom of the
// sidebar), or null without a vertical tab strip.
views::View* GetSidebarContentColumn(BrowserView* browser_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_VERTICAL_TABS_POLICY_H_

// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SHORTCUTS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SHORTCUTS_H_

#include <optional>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/koan_shortcut_table.h"
#include "ui/base/accelerators/accelerator.h"

class BrowserView;
class PrefService;
class TabStripModel;

namespace views {
class FocusManager;
}

namespace koan {

class KoanBrowserViewController;

// Koan's keyboard shortcuts: Zen's defaults (zen-browser/desktop
// src/zen/kbs/ZenKeyboardShortcuts.mjs, shortcuts version 21, Linux), plus a
// few Koan-only ones, as rebound in Koan's settings (the table is in
// koan/browser/koan_shortcut_table.h), and Zen's Ctrl+Tab settings.
// Registered on the window's FocusManager
// after BrowserView's own accelerators, so they are tried first; where Zen
// deliberately rebinds a key Chrome uses (Ctrl+S, Ctrl+Shift+C/D/N/P), Koan
// follows Zen. A contextual shortcut (Ctrl+O while Glance is open) passes
// the key on to Chrome otherwise. The full table is in docs/USAGE.md.
class KoanShortcutController : public ui::AcceleratorTarget {
 public:
  using Action = shortcuts::Action;
  using Binding = shortcuts::Binding;

  KoanShortcutController(BrowserView* browser_view,
                         KoanBrowserViewController* controller);
  KoanShortcutController(const KoanShortcutController&) = delete;
  KoanShortcutController& operator=(const KoanShortcutController&) = delete;
  ~KoanShortcutController() override;

  void OnAddedToWidget();
  void OnBrowserViewDestroying();

  // Runs `action`; returns false if it doesn't apply right now (the key then
  // goes on to Chrome's own handler).
  bool Run(Action action);
  // Whether `action` applies right now (its feature is on and there is
  // something to act on), for menus that show it enabled or not. Run() may
  // still return false for one that can: it also checks the page's state.
  bool CanRun(Action action) const;

  // Closes the unpinned tabs of the focused workspace (or, with none focused,
  // the tabs in no workspace), like Zen's "Clear" in the sidebar
  // (koan_sidebar_tabs.cc), and toasts about it. The workspace survives, and
  // Ctrl+Shift+T brings the tabs back.
  bool CloseUnpinnedTabs();

  // The indices of the tabs CloseUnpinnedTabs considers: unpinned, in the
  // focused workspace or, with none focused, in no tab group.
  static std::vector<int> UnpinnedTabsForClear(const TabStripModel* model);

  // Whether CloseUnpinnedTabs closes the tab at `index`: like Zen, it keeps
  // the selected tabs and those playing sound, unless that is all of them.
  static bool IsClosableByClear(const TabStripModel* model, int index);

  // The keys in effect, for menus that show them.
  const std::vector<Binding>& bindings() const { return bindings_; }
  // The key currently bound to `action` (the user may rebind it), if any.
  std::optional<ui::Accelerator> GetAccelerator(Action action) const;
  // Whether `accelerator` runs a Koan shortcut instead of Chrome's command.
  bool IsBound(const ui::Accelerator& accelerator) const;

  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

 private:
  bool Split(int layout_index);
  bool Unsplit();
  bool CopyUrl(bool markdown);
  bool TogglePinTab();
  bool Command(int command_id);
  // Zen's Ctrl+Tab settings; returns false (Chrome cycles) when both are off.
  bool CycleTabs(const shortcuts::CycleKey& key);
  // Ctrl+Tab in recently used order, as Firefox's ctrlTab that Zen keeps
  // (browser-ctrlTab.js; Zen's patch only narrows its list): the first press
  // goes to the tab shown last before the active one, of those Zen's list
  // has; each further press while Ctrl is held goes on down the same list
  // (Shift goes back up), and letting go of Ctrl ends the walk, so the next
  // Ctrl+Tab flips back again. (Off macOS, Ctrl counts as let go of after
  // every press: IsControlKeyDown().)
  void CycleToRecentTab(TabStripModel* model, bool forward);
  // The tabs of Zen's list, most recently used first: the active one first.
  static std::vector<tabs::TabHandle> RecentTabs(TabStripModel* model);
  // Ends the walk once Ctrl is let go of.
  void CheckControlReleased();

  // Koan's prefs (the regular profile's, also in incognito).
  PrefService* GetPrefs() const;

  // Registers `bindings_` with the window's FocusManager.
  void RegisterAccelerators();
  // Picks up the user's shortcuts after a change in the settings page.
  void OnShortcutsChanged();

  const raw_ptr<BrowserView> browser_view_;
  const raw_ptr<KoanBrowserViewController> controller_;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  std::vector<Binding> bindings_;
  PrefChangeRegistrar pref_change_registrar_;
  // The walk of CycleToRecentTab(): the list as it was at its first press,
  // and where in it the walk is.
  std::vector<tabs::TabHandle> recent_walk_;
  size_t recent_walk_index_ = 0;
  base::RepeatingTimer control_release_timer_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SHORTCUTS_H_

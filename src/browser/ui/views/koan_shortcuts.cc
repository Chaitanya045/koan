// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_shortcuts.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_keyboard_state.h"
#include "koan/browser/ui/views/koan_library.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_sidebar_tabs_controller.h"
#include "koan/browser/ui/views/koan_split_view.h"
#include "koan/browser/ui/views/koan_tab_unload.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/common/koan_url_constants.h"
#if BUILDFLAG(IS_MAC)
#include "koan/browser/ui/cocoa/koan_main_menu.h"
#endif
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "url/gurl.h"

namespace koan {

namespace {

// Actions that open a window, a tab or a form, which a held key must not
// repeat.
bool CreatesSomething(shortcuts::Action action) {
  switch (action) {
    case shortcuts::Action::kNewTab:
    case shortcuts::Action::kNewWindow:
    case shortcuts::Action::kNewPrivateWindow:
    case shortcuts::Action::kNewWorkspace:
    case shortcuts::Action::kNewEmptySplit:
      return true;
    default:
      return false;
  }
}

}  // namespace

KoanShortcutController::KoanShortcutController(
    BrowserView* browser_view,
    KoanBrowserViewController* controller)
    : browser_view_(browser_view), controller_(controller) {
  bindings_ = shortcuts::Bindings(GetPrefs()->GetDict(prefs::kShortcuts));
  pref_change_registrar_.Init(GetPrefs());
  pref_change_registrar_.Add(
      prefs::kShortcuts,
      base::BindRepeating(&KoanShortcutController::OnShortcutsChanged,
                          base::Unretained(this)));
  // Back's enabled state depends on this setting (back-to-opener).
  pref_change_registrar_.Add(
      prefs::kCloseOnBackWithNoHistory,
      base::BindRepeating(
          [](BrowserView* browser_view) {
            chrome::BrowserCommandController::From(browser_view->browser())
                ->TabStateChanged();
          },
          base::Unretained(browser_view_.get())));
}

PrefService* KoanShortcutController::GetPrefs() const {
  return browser_view_->GetProfile()->GetOriginalProfile()->GetPrefs();
}

KoanShortcutController::~KoanShortcutController() {
  OnBrowserViewDestroying();
}

void KoanShortcutController::OnAddedToWidget() {
  if (focus_manager_ || !browser_view_->GetFocusManager()) {
    return;
  }
  focus_manager_ = browser_view_->GetFocusManager();
  RegisterAccelerators();
}

void KoanShortcutController::RegisterAccelerators() {
  for (const Binding& binding : bindings_) {
    auto priority = ui::AcceleratorManager::kNormalPriority;
#if BUILDFLAG(IS_MAC)
    // On macOS Chrome runs its reserved commands (Cmd+Shift+N, Cmd+T, Cmd+W,
    // Cmd+Option+arrows ...) in prePerformKeyEquivalent, before normal
    // priority accelerators: a Koan shortcut on one of those keys, like Zen's
    // Space keys (Chrome's tab switching) and Cmd+Shift+N, is high priority
    // to come first. So are the Library's History and Downloads keys (Cmd+Y,
    // Cmd+Shift+H, Cmd+Shift+J), which are Chrome's menu keys for History,
    // Home and Downloads. (The others stay behind text fields and pages.)
    if (shortcuts::IsReservedOnMac(binding.accelerator) ||
        binding.action == Action::kLibraryHistory ||
        binding.action == Action::kDownloads) {
      priority = ui::AcceleratorManager::kHighPriority;
    }
#endif
    // Normal priority targets are tried most-recently-registered first, so
    // these run before BrowserView's own accelerators for the same keys.
    focus_manager_->RegisterAccelerator(binding.accelerator, priority, this);
  }
  // Chrome's tab cycling keys: Koan cycles itself while one of Zen's Ctrl+Tab
  // settings is on, and passes the keys on to Chrome otherwise. No binding
  // uses them (shortcuts::IsValidCustomAccelerator()).
  for (const shortcuts::CycleKey& key : shortcuts::CycleKeys()) {
    focus_manager_->RegisterAccelerator(ui::Accelerator(key.key, key.modifiers),
                                        ui::AcceleratorManager::kNormalPriority,
                                        this);
  }
}

void KoanShortcutController::OnShortcutsChanged() {
  bindings_ = shortcuts::Bindings(GetPrefs()->GetDict(prefs::kShortcuts));
  if (focus_manager_) {
    focus_manager_->UnregisterAccelerators(this);
    RegisterAccelerators();
  }
#if BUILDFLAG(IS_MAC)
  // The menu bar's keys follow, or the old key would still run the action
  // through the menu.
  RefreshMainMenuKeys();
#endif
}

void KoanShortcutController::OnBrowserViewDestroying() {
  pref_change_registrar_.RemoveAll();
  if (focus_manager_) {
    focus_manager_->UnregisterAccelerators(this);
    focus_manager_ = nullptr;
  }
}

bool KoanShortcutController::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  for (const Binding& binding : bindings_) {
    if (binding.accelerator == accelerator) {
      // Like Chrome's New Tab and New Window, a held key doesn't repeat: it
      // would open and close the URL bar, or open window after window. (On
      // macOS Chrome drops those repeats after the high priority
      // accelerators, which these come before.)
      if (accelerator.IsRepeat() && CreatesSomething(binding.action)) {
        return true;
      }
      return Run(binding.action);
    }
  }
  for (const shortcuts::CycleKey& key : shortcuts::CycleKeys()) {
    if (accelerator == ui::Accelerator(key.key, key.modifiers)) {
      return CycleTabs(key);
    }
  }
  return false;
}

bool KoanShortcutController::CanHandleAccelerators() const {
  return browser_view_->GetIsNormalType();
}

bool KoanShortcutController::CanRun(Action action) const {
  const shortcuts::Info* info = nullptr;
  for (const shortcuts::Info& candidate : shortcuts::Actions()) {
    if (candidate.action == action) {
      info = &candidate;
      break;
    }
  }
  if (!info || (info->is_available && !info->is_available())) {
    return false;
  }
  const KoanWorkspaceController* workspaces = controller_->workspaces();
  BrowserWindowInterface* browser = browser_view_->browser();
  tabs::TabInterface* tab = browser->GetTabStripModel()->GetActiveTab();
  if (const int number = shortcuts::SwitchWorkspaceNumber(action)) {
    return workspaces->IsEnabled() &&
           number <= static_cast<int>(workspaces->GetSpaces().size());
  }
  switch (action) {
    case Action::kWorkspaceForward:
    case Action::kWorkspaceBackward:
      return workspaces->IsEnabled() && workspaces->GetSpaces().size() > 1;
    case Action::kNewWorkspace:
      return workspaces->IsEnabled();
    case Action::kUnsplit:
      return tab && tab->IsSplit();
    case Action::kNewEmptySplit:
      return chrome::IsCommandEnabled(browser, IDC_NEW_SPLIT_TAB);
    case Action::kSplitGrid:
    case Action::kSplitVertical:
    case Action::kSplitHorizontal:
    case Action::kCopyUrl:
    case Action::kCopyUrlMarkdown:
    case Action::kTogglePinTab:
      return tab != nullptr;
    case Action::kResetPinnedTab:
      return tab && IsPinnedForUser(tab);
    case Action::kGlanceExpand:
      return controller_->glance()->is_open() &&
             !controller_->glance()->is_closing();
    case Action::kSavePage:
      return chrome::IsCommandEnabled(browser, IDC_SAVE_PAGE);
    case Action::kLibrary:
      return controller_->library()->IsAvailable();
    case Action::kInspect:
      return chrome::IsCommandEnabled(browser, IDC_DEV_TOOLS_INSPECT);
    case Action::kNewWindow:
      return chrome::IsCommandEnabled(browser, IDC_NEW_WINDOW);
    case Action::kNewPrivateWindow:
      return chrome::IsCommandEnabled(browser, IDC_NEW_INCOGNITO_WINDOW);
    case Action::kMuteSite:
      return chrome::IsCommandEnabled(browser, IDC_WINDOW_MUTE_SITE);
    case Action::kBookmarks:
      return chrome::IsCommandEnabled(browser, IDC_SHOW_BOOKMARK_SIDE_PANEL);
    case Action::kDownloads:
      return chrome::IsCommandEnabled(browser, IDC_SHOW_DOWNLOADS);
    case Action::kReaderMode:
      return chrome::IsCommandEnabled(browser, IDC_SHOW_READING_MODE);
    default:
      return true;
  }
}

bool KoanShortcutController::Run(Action action) {
  KoanCompactModeController* compact = controller_->compact_mode();
  KoanWorkspaceController* workspaces = controller_->workspaces();
  KoanLibraryController* library = controller_->library();
  // Shortcuts for a Koan feature switched off in chrome://flags do nothing
  // (and let the key through), like the feature itself.
  switch (action) {
    case Action::kToggleCompactMode:
    case Action::kCompactModeShowSidebar:
    case Action::kToggleSingleToolbar:
      if (!features::IsCompactModeEnabled()) {
        return false;
      }
      break;
    case Action::kWorkspaceForward:
    case Action::kWorkspaceBackward:
    case Action::kNewWorkspace:
      if (!workspaces->IsEnabled()) {
        return false;
      }
      break;
    default:
      if (shortcuts::SwitchWorkspaceNumber(action) &&
          !workspaces->IsEnabled()) {
        return false;
      }
      break;
  }
  if (const int number = shortcuts::SwitchWorkspaceNumber(action)) {
    // Without that many Spaces the key goes on to Chrome.
    return workspaces->SwitchToNumber(number);
  }
  switch (action) {
    case Action::kToggleCompactMode:
      compact->ToggleCompactMode();
      return true;
    case Action::kCompactModeShowSidebar:
      return compact->PeekSidebar();
    case Action::kToggleSingleToolbar:
      compact->ToggleSingleToolbar();
      return true;
    case Action::kWorkspaceForward:
      workspaces->Navigate(/*forward=*/true);
      return true;
    case Action::kWorkspaceBackward:
      workspaces->Navigate(/*forward=*/false);
      return true;
    case Action::kNewWorkspace:
      workspaces->ShowCreateForm();
      return true;
    case Action::kSplitGrid:
      // Chromium's split view has two panes; Zen's grid becomes side by side.
    case Action::kSplitVertical:
      return Split(static_cast<int>(split_tabs::SplitTabLayout::kSideBySide));
    case Action::kSplitHorizontal:
      return Split(static_cast<int>(split_tabs::SplitTabLayout::kStacked));
    case Action::kUnsplit:
      return Unsplit();
    case Action::kNewEmptySplit:
      return Command(IDC_NEW_SPLIT_TAB);
    case Action::kNewTab:
      return controller_->url_bar()->OpenForNewTab();
    case Action::kCopyUrl:
      return CopyUrl(/*markdown=*/false);
    case Action::kCopyUrlMarkdown:
      return CopyUrl(/*markdown=*/true);
    case Action::kTogglePinTab:
      return TogglePinTab();
    case Action::kResetPinnedTab: {
      tabs::TabInterface* tab =
          browser_view_->browser()->GetTabStripModel()->GetActiveTab();
      if (!features::IsPinnedTabsEnabled() || !IsPinnedForUser(tab)) {
        return false;
      }
      controller_->pinned_tabs()->ResetToPinnedUrl(tab);
      return true;
    }
    case Action::kCloseUnpinnedTabs:
      return CloseUnpinnedTabs();
    case Action::kGlanceExpand:
      // Only while Glance is open; otherwise Ctrl+O stays "Open file".
      // (Not while it shrinks away: the key goes to the page then.)
      if (!controller_->glance()->is_open() ||
          controller_->glance()->is_closing()) {
        return false;
      }
      controller_->glance()->PostAction(&KoanGlanceController::ExpandToTab);
      return true;
    case Action::kSavePage:
      return Command(IDC_SAVE_PAGE);
    case Action::kLibrary:
      // No Library (flag off, or a private window): the key goes on.
      if (!library->IsAvailable()) {
        return false;
      }
      library->Toggle();
      return true;
    case Action::kLibraryHistory:
      if (!library->IsAvailable()) {
#if BUILDFLAG(IS_MAC)
        // The key goes on to Chrome's menu (History, or Home for
        // Cmd+Shift+H).
        return false;
#else
        return Command(IDC_SHOW_HISTORY);
#endif
      }
      library->Toggle(LibrarySection::kHistory);
      return true;
    case Action::kInspect:
      return Command(IDC_DEV_TOOLS_INSPECT);
    case Action::kNewWindow:
      return Command(IDC_NEW_WINDOW);
    case Action::kNewPrivateWindow:
      return Command(IDC_NEW_INCOGNITO_WINDOW);
    case Action::kMuteSite:
      return Command(IDC_WINDOW_MUTE_SITE);
    case Action::kBookmarks:
      return Command(IDC_SHOW_BOOKMARK_SIDE_PANEL);
    case Action::kDownloads:
      if (!library->IsAvailable()) {
#if BUILDFLAG(IS_MAC)
        // The key goes on to Chrome's menu (Downloads).
        return false;
#else
        return Command(IDC_SHOW_DOWNLOADS);
#endif
      }
      library->Toggle(LibrarySection::kDownloads);
      return true;
    case Action::kReaderMode:
      return Command(IDC_SHOW_READING_MODE);
    case Action::kSwitchWorkspace1:
    case Action::kSwitchWorkspace2:
    case Action::kSwitchWorkspace3:
    case Action::kSwitchWorkspace4:
    case Action::kSwitchWorkspace5:
    case Action::kSwitchWorkspace6:
    case Action::kSwitchWorkspace7:
    case Action::kSwitchWorkspace8:
    case Action::kSwitchWorkspace9:
    case Action::kSwitchWorkspace10:
      // Handled above.
      return false;
    case Action::kOpenSettings:
      // Like chrome://settings: one tab whatever pane it shows, and in a
      // regular window (Koan's settings are the regular profile's).
      ShowSingletonTabOverwritingNTP(
          browser_view_->GetProfile()->GetOriginalProfile(), GURL(kSettingsURL),
          NavigateParams::IGNORE_AND_NAVIGATE);
      return true;
  }
  return false;
}

bool KoanShortcutController::CycleTabs(const shortcuts::CycleKey& key) {
  BrowserWindowInterface* browser = browser_view_->browser();
  const PrefService* prefs = GetPrefs();
  const bool within_kind = prefs->GetBoolean(prefs::kCtrlTabWithinKind);
  const bool skip_pending = prefs->GetBoolean(prefs::kCtrlTabSkipPending);
  // Chromium's "Ctrl+Tab in recently used order" (chrome://settings >
  // Appearance) wins for Ctrl+Tab, as in Zen, whose settings page warns
  // about the combination; the Koan rows show the same note.
  const bool mru =
      key.key == ui::VKEY_TAB && chrome::IsCtrlTabMruEnabled(browser);
  if (!mru && !within_kind && !skip_pending) {
    return false;
  }
  // Without tab switching (e.g. a popup window) the key goes to the page.
  if (!chrome::IsCommandEnabled(browser, key.forward
                                             ? IDC_SELECT_NEXT_TAB
                                             : IDC_SELECT_PREVIOUS_TAB)) {
    return false;
  }
  TabStripModel* model = browser->GetTabStripModel();
  const int active = model->active_index();
  if (active == TabStripModel::kNoTab) {
    return false;
  }
  if (mru) {
    CycleToRecentTab(model, key.forward);
    return true;
  }
  const bool forward = key.forward;
  // The tabs Chrome would cycle through (the workspace's and the
  // Essentials), narrowed by Zen's settings.
  const std::optional<tab_groups::TabGroupId> workspace =
      model->GetFocusedGroup();
  const bool active_pinned = model->IsTabPinned(active);
  const int count = model->count();
  for (int step = 1; step < count; ++step) {
    const int i = (active + (forward ? step : count - step)) % count;
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (model->IsTabCollapsed(i) ||
        (workspace && !tab->IsPinned() && tab->GetGroup() != workspace) ||
        (within_kind && tab->IsPinned() != active_pinned) ||
        (skip_pending && IsTabUnloaded(tab->GetContents()))) {
      continue;
    }
    model->ActivateTabAt(
        i, TabStripUserGestureDetails(
               TabStripUserGestureDetails::GestureType::kKeyboard));
    return true;
  }
  return true;
}

// static
std::vector<tabs::TabHandle> KoanShortcutController::RecentTabs(
    TabStripModel* model) {
  // Zen's recently-used list (browser-ctrlTab-js.patch) holds the tabs that
  // are shown, so this Space's and the Essentials, and never one that hasn't
  // loaded (zen.ctrlTab.show-pending-tabs, false). Chromium's
  // (chrome::CycleToMruTab) takes every tab of the window, and would open
  // another Space's collapsed group to show one.
  const std::optional<tab_groups::TabGroupId> workspace =
      model->GetFocusedGroup();
  std::vector<tabs::TabInterface*> tabs;
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (i != model->active_index() &&
        (model->IsTabCollapsed(i) ||
         (workspace && !tab->IsPinned() && tab->GetGroup() != workspace) ||
         IsTabUnloaded(tab->GetContents()))) {
      continue;
    }
    tabs.push_back(tab);
  }
  // (The active tab was shown last of all.)
  tabs::TabInterface* active = model->GetActiveTab();
  std::ranges::stable_sort(
      tabs, [active](tabs::TabInterface* a, tabs::TabInterface* b) {
        if (a == active || b == active) {
          return a == active && b != active;
        }
        return a->GetContents()->GetLastActiveTime() >
               b->GetContents()->GetLastActiveTime();
      });
  std::vector<tabs::TabHandle> handles;
  for (tabs::TabInterface* tab : tabs) {
    handles.push_back(tab->GetHandle());
  }
  return handles;
}

void KoanShortcutController::CycleToRecentTab(TabStripModel* model,
                                              bool forward) {
  tabs::TabInterface* active = model->GetActiveTab();
  // A new walk, unless this press continues one: Ctrl still held since the
  // last, and the tab it showed still the active one.
  const bool continues =
      !recent_walk_.empty() && IsControlKeyDown() && active &&
      recent_walk_[recent_walk_index_] == active->GetHandle();
  if (!continues) {
    recent_walk_ = RecentTabs(model);
    recent_walk_index_ = 0;
    // The first press goes to the last tab shown, Shift or not (Firefox opens
    // its all-tabs list on a first Ctrl+Shift+Tab instead).
    forward = true;
  }
  const size_t count = recent_walk_.size();
  // Firefox's advanceFocus(): on round the list, the active tab (its first)
  // included; a tab that has gone since the walk began is passed over.
  for (size_t step = 0; step + 1 < count; ++step) {
    recent_walk_index_ =
        (recent_walk_index_ + (forward ? 1 : count - 1)) % count;
    tabs::TabInterface* tab = recent_walk_[recent_walk_index_].Get();
    const int index = tab ? model->GetIndexOfTab(tab) : TabStripModel::kNoTab;
    if (index == TabStripModel::kNoTab) {
      continue;
    }
    if (tab != active) {
      model->ActivateTabAt(
          index, TabStripUserGestureDetails(
                     TabStripUserGestureDetails::GestureType::kKeyboard));
    }
    break;
  }
  if (IsControlKeyDown()) {
    control_release_timer_.Start(FROM_HERE, base::Milliseconds(50), this,
                                 &KoanShortcutController::CheckControlReleased);
  } else {
    recent_walk_.clear();
  }
}

void KoanShortcutController::CheckControlReleased() {
  if (!IsControlKeyDown()) {
    control_release_timer_.Stop();
    recent_walk_.clear();
  }
}

bool KoanShortcutController::IsBound(const ui::Accelerator& accelerator) const {
  return std::ranges::any_of(bindings_, [&](const Binding& binding) {
    return binding.accelerator == accelerator;
  });
}

bool KoanShortcutController::Command(int command_id) {
  BrowserWindowInterface* browser = browser_view_->browser();
  if (!chrome::IsCommandEnabled(browser, command_id)) {
    return false;
  }
  chrome::ExecuteCommand(browser, command_id);
  return true;
}

bool KoanShortcutController::Split(int layout_index) {
  const auto layout = static_cast<split_tabs::SplitTabLayout>(layout_index);
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  const int active = model->active_index();
  if (active == TabStripModel::kNoTab) {
    return false;
  }
  tabs::TabInterface* active_tab = model->GetTabAtIndex(active);
  if (active_tab->IsSplit()) {
    // Zen: with another (unsplit) tab selected this would join a third
    // panel into the split view, which Chromium's two panes can't take;
    // Zen shows its split-view limit toast then. With nothing extra
    // selected, the layout is switched instead.
    for (int i = 0; i < model->count(); ++i) {
      if (i != active && model->IsTabSelected(i) &&
          !model->GetTabAtIndex(i)->IsSplit()) {
        ShowSplitLimitToast(browser_view_);
        return true;
      }
    }
    model->UpdateSplitLayout(*active_tab->GetSplit(), layout);
    return true;
  }
  // Split the selected tabs (Zen's behaviour with a multi-selection), else
  // the active tab with the next one; with nothing to pair, a new tab.
  std::vector<int> others;
  for (int i = 0; i < model->count(); ++i) {
    if (i != active && model->IsTabSelected(i) &&
        !model->GetTabAtIndex(i)->IsSplit()) {
      others.push_back(i);
    }
  }
  if (others.size() > 1) {
    // Zen splits up to four tabs; Chromium's split view takes two.
    ShowSplitLimitToast(browser_view_);
    return true;
  }
  if (others.empty() && active + 1 < model->count() &&
      !model->GetTabAtIndex(active + 1)->IsSplit() &&
      model->IsTabPinned(active + 1) == model->IsTabPinned(active)) {
    others.push_back(active + 1);
  }
  if (others.empty()) {
    chrome::NewSplitTab(browser_view_->browser(), layout,
                        split_tabs::SplitTabCreatedSource::kKeyboardShortcut);
    return true;
  }
  model->AddToNewSplit(others, split_tabs::SplitTabVisualData(layout),
                       split_tabs::SplitTabCreatedSource::kKeyboardShortcut);
  return true;
}

bool KoanShortcutController::Unsplit() {
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  tabs::TabInterface* tab = model->GetActiveTab();
  if (!tab || !tab->IsSplit()) {
    return false;
  }
  model->RemoveSplit(*tab->GetSplit());
  return true;
}

bool KoanShortcutController::CopyUrl(bool markdown) {
  if (!browser_view_->browser()->GetTabStripModel()->GetActiveWebContents()) {
    return false;
  }
  CopyCurrentUrl(browser_view_, markdown);
  return true;
}

bool KoanShortcutController::TogglePinTab() {
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  const int active = model->active_index();
  if (active == TabStripModel::kNoTab) {
    return false;
  }
  if (features::IsPinnedTabsEnabled()) {
    // Zen: pins the tab to its Space (an Essential leaves the Essentials).
    controller_->pinned_tabs()->TogglePin(model->GetTabAtIndex(active));
    return true;
  }
  model->SetTabPinned(active, !model->IsTabPinned(active));
  return true;
}

std::optional<ui::Accelerator> KoanShortcutController::GetAccelerator(
    Action action) const {
  for (const Binding& binding : bindings_) {
    if (binding.action == action) {
      return binding.accelerator;
    }
  }
  return std::nullopt;
}

bool KoanShortcutController::CloseUnpinnedTabs() {
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  const std::vector<int> unpinned = UnpinnedTabsForClear(model);
  if (unpinned.empty()) {
    return true;
  }
  std::vector<int> to_close;
  std::ranges::copy_if(
      unpinned, std::back_inserter(to_close),
      [model](int index) { return IsClosableByClear(model, index); });
  // Nothing but kept tabs: Zen closes them all, the selected one included.
  const bool closes_selected = to_close.empty();
  if (closes_selected) {
    to_close = unpinned;
  }
  // A workspace is a tab group, which goes away with its last tab; Zen's
  // space survives being emptied. Leave it (or, closing the selected tab, the
  // window) a fresh tab.
  const std::optional<tab_groups::TabGroupId> workspace =
      model->GetFocusedGroup();
  const bool empties_workspace =
      workspace &&
      static_cast<int>(to_close.size()) ==
          model->group_model()->GetTabGroup(*workspace)->tab_count();
  if (closes_selected || empties_workspace) {
    chrome::AddTabAt(browser_view_->browser(), GURL(), to_close.back() + 1,
                     /*foreground=*/closes_selected, workspace);
  }
  for (auto it = to_close.rbegin(); it != to_close.rend(); ++it) {
    // Into the history, so Ctrl+Shift+T restores them.
    model->CloseWebContentsAt(*it, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB |
                                       TabCloseTypes::CLOSE_USER_GESTURE);
  }
  controller_->sidebar_tabs()->ShowTabsClosedToast();
  return true;
}

// static
std::vector<int> KoanShortcutController::UnpinnedTabsForClear(
    const TabStripModel* model) {
  // Zen's Clear closes the space's unpinned tabs. With no workspace focused
  // ("all tabs"), the tabs in workspaces belong to those, so it leaves them.
  const std::optional<tab_groups::TabGroupId> workspace =
      model->GetFocusedGroup();
  std::vector<int> indices;
  for (int i = 0; i < model->count(); ++i) {
    // Pinned tabs (Essentials, and those pinned to the workspace) stay.
    if (!IsPinnedForUser(model->GetTabAtIndex(i)) &&
        model->GetTabGroupForTab(i) == workspace) {
      indices.push_back(i);
    }
  }
  return indices;
}

// static
bool KoanShortcutController::IsClosableByClear(const TabStripModel* model,
                                               int index) {
  return !model->IsTabSelected(index) &&
         !model->GetWebContentsAt(index)->IsCurrentlyAudible();
}

}  // namespace koan

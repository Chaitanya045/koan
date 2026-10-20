// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_shortcuts.h"

#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_glance_controller.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "koan/common/koan_url_constants.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "url/gurl.h"

namespace koan {

namespace {

// Zen's "pending" tabs: not loaded yet (discarded, or restored and not
// shown since).
bool IsPending(content::WebContents* contents) {
  return contents->WasDiscarded() || contents->GetController().NeedsReload();
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
    // Normal priority targets are tried most-recently-registered first, so
    // these run before BrowserView's own accelerators for the same keys.
    focus_manager_->RegisterAccelerator(
        binding.accelerator, ui::AcceleratorManager::kNormalPriority, this);
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

bool KoanShortcutController::Run(Action action) {
  KoanCompactModeController* compact = controller_->compact_mode();
  KoanWorkspaceController* workspaces = controller_->workspaces();
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
      break;
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
      workspaces->Cycle(/*forward=*/true);
      return true;
    case Action::kWorkspaceBackward:
      workspaces->Cycle(/*forward=*/false);
      return true;
    case Action::kNewWorkspace:
      workspaces->CreateWorkspace();
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
    case Action::kCopyUrl:
      return CopyUrl(/*markdown=*/false);
    case Action::kCopyUrlMarkdown:
      return CopyUrl(/*markdown=*/true);
    case Action::kTogglePinTab:
      return TogglePinTab();
    case Action::kCloseUnpinnedTabs:
      return CloseUnpinnedTabs();
    case Action::kGlanceExpand:
      // Only while Glance is open; otherwise Ctrl+O stays "Open file".
      if (!controller_->glance()->is_open()) {
        return false;
      }
      controller_->glance()->PostAction(&KoanGlanceController::ExpandToTab);
      return true;
    case Action::kSavePage:
      return Command(IDC_SAVE_PAGE);
    case Action::kLibrary:
      return Command(IDC_SHOW_HISTORY);
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
      return Command(IDC_SHOW_DOWNLOADS);
    case Action::kReaderMode:
      return Command(IDC_SHOW_READING_MODE);
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
  if (!within_kind && !skip_pending) {
    return false;
  }
  // Without tab switching (e.g. a popup window) the key goes to the page.
  if (!chrome::IsCommandEnabled(browser, key.forward
                                             ? IDC_SELECT_NEXT_TAB
                                             : IDC_SELECT_PREVIOUS_TAB)) {
    return false;
  }
  // Chromium's "Ctrl+Tab in recently used order" (chrome://settings >
  // Appearance) wins for Ctrl+Tab, as in Zen, whose settings page warns
  // about the combination; the Koan rows show the same note.
  if (key.key == ui::VKEY_TAB && chrome::IsCtrlTabMruEnabled(browser)) {
    return false;
  }
  TabStripModel* model = browser->GetTabStripModel();
  const int active = model->active_index();
  if (active == TabStripModel::kNoTab) {
    return false;
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
        (skip_pending && IsPending(tab->GetContents()))) {
      continue;
    }
    model->ActivateTabAt(
        i, TabStripUserGestureDetails(
               TabStripUserGestureDetails::GestureType::kKeyboard));
    return true;
  }
  return true;
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
    // Already split: switch the layout, like Zen.
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
      break;
    }
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
  BrowserWindowInterface* browser = browser_view_->browser();
  content::WebContents* contents =
      browser->GetTabStripModel()->GetActiveWebContents();
  if (!contents) {
    return false;
  }
  if (!markdown) {
    // Also shows Chrome's "Link copied" toast.
    chrome::CopyURL(browser, contents);
    return true;
  }
  std::u16string title = contents->GetTitle();
  if (title.empty()) {
    title = base::UTF8ToUTF16(contents->GetVisibleURL().spec());
  }
  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteText(base::StrCat(
      {u"[", title, u"](", base::UTF8ToUTF16(contents->GetVisibleURL().spec()),
       u")"}));
  return true;
}

bool KoanShortcutController::TogglePinTab() {
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  const int active = model->active_index();
  if (active == TabStripModel::kNoTab) {
    return false;
  }
  model->SetTabPinned(active, !model->IsTabPinned(active));
  return true;
}

bool KoanShortcutController::CloseUnpinnedTabs() {
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  // In a workspace, only its tabs (Zen closes the workspace's unpinned tabs).
  const std::optional<tab_groups::TabGroupId> workspace =
      model->GetFocusedGroup();
  std::vector<int> to_close;
  for (int i = 0; i < model->count(); ++i) {
    if (model->IsTabPinned(i)) {
      continue;
    }
    if (workspace && model->GetTabGroupForTab(i) != workspace) {
      continue;
    }
    to_close.push_back(i);
  }
  if (to_close.empty()) {
    return true;
  }
  // Leave the window (and workspace) with a fresh tab, like Zen.
  chrome::AddTabAt(browser_view_->browser(), GURL(), to_close.back() + 1,
                   /*foreground=*/true, workspace);
  for (auto it = to_close.rbegin(); it != to_close.rend(); ++it) {
    model->CloseWebContentsAt(*it, TabCloseTypes::CLOSE_USER_GESTURE);
  }
  return true;
}

}  // namespace koan

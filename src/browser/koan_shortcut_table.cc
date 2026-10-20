// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_shortcut_table.h"

#include <algorithm>
#include <set>
#include <string_view>

#include "koan/browser/koan_features.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace koan::shortcuts {

namespace {

constexpr int kCtrl = ui::EF_CONTROL_DOWN;
constexpr int kAlt = ui::EF_ALT_DOWN;
constexpr int kShift = ui::EF_SHIFT_DOWN;
constexpr int kModifiers = kCtrl | kAlt | kShift | ui::EF_COMMAND_DOWN;

constexpr char kKey[] = "key";
constexpr char kModifiersKey[] = "modifiers";

constexpr Group kGroups[] = {
    {"zen-compact-mode", "Compact Mode"},
    {"zen-workspace", "Workspaces"},
    {"zen-split-view", "Split View"},
    {"zen-other", "Other Zen Features"},
    {"windowAndTabManagement", "Window & Tab Management"},
    {"navigation", "Navigation"},
    {"pageOperations", "Page Operations"},
    {"historyAndBookmarks", "History & Bookmarks"},
    {"mediaAndDisplay", "Media & Display"},
    {"devTools", "Developer Tools"},
    {"other", "Other"},
};

constexpr Info kActions[] = {
    {Action::kToggleCompactMode, "compact-mode-toggle", "zen-compact-mode",
     "Toggle Compact Mode", features::IsCompactModeEnabled},
    {Action::kCompactModeShowSidebar, "compact-mode-show-sidebar",
     "zen-compact-mode", "Toggle Floating Sidebar",
     features::IsCompactModeEnabled},
    {Action::kWorkspaceForward, "workspace-forward", "zen-workspace",
     "Forward Workspace", features::IsWorkspacesEnabled},
    {Action::kWorkspaceBackward, "workspace-backward", "zen-workspace",
     "Backward Workspace", features::IsWorkspacesEnabled},
    {Action::kNewWorkspace, "workspace-create", "zen-workspace",
     "Create New Workspace", features::IsWorkspacesEnabled},
    {Action::kSplitGrid, "split-view-grid", "zen-split-view",
     "Toggle Split View Grid"},
    {Action::kSplitVertical, "split-view-vertical", "zen-split-view",
     "Toggle Split View Vertical"},
    {Action::kSplitHorizontal, "split-view-horizontal", "zen-split-view",
     "Toggle Split View Horizontal"},
    {Action::kUnsplit, "split-view-unsplit", "zen-split-view",
     "Close Split View"},
    {Action::kNewEmptySplit, "new-empty-split-view", "zen-split-view",
     "New Empty Split View"},
    {Action::kCopyUrl, "copy-url", "zen-other", "Copy current URL"},
    {Action::kCopyUrlMarkdown, "copy-url-markdown", "zen-other",
     "Copy current URL as Markdown"},
    {Action::kTogglePinTab, "toggle-pin-tab", "zen-other", "Toggle Pin Tab"},
    {Action::kGlanceExpand, "glance-expand", "zen-other", "Expand Glance",
     features::IsGlanceEnabled},
    {Action::kLibrary, "library", "zen-other", "Toggle Library"},
    {Action::kToggleSingleToolbar, "toggle-single-toolbar", "zen-other",
     "Switch Single / Multiple Toolbars", features::IsCompactModeEnabled},
    {Action::kNewWindow, "new-window", "windowAndTabManagement", "New Window"},
    {Action::kCloseUnpinnedTabs, "close-unpinned-tabs",
     "windowAndTabManagement", "Close All Unpinned Tabs"},
    {Action::kNewPrivateWindow, "private-browsing", "navigation",
     "Private Browsing"},
    {Action::kSavePage, "save-page", "pageOperations", "Save Page"},
    {Action::kReaderMode, "reader-mode", "pageOperations",
     "Toggle Reader Mode"},
    {Action::kBookmarks, "bookmarks", "historyAndBookmarks",
     "Show Bookmarks Sidebar"},
    {Action::kMuteSite, "mute", "mediaAndDisplay", "Toggle Mute"},
    {Action::kInspect, "inspector", "devTools", "Toggle Inspector"},
    {Action::kDownloads, "downloads", "other", "Open Downloads"},
    {Action::kOpenSettings, "open-settings", "other", "Open Preferences"},
};

}  // namespace

base::span<const Group> Groups() {
  return kGroups;
}

base::span<const Info> Actions() {
  return kActions;
}

const Info* FindAction(std::string_view id) {
  for (const Info& info : kActions) {
    if (id == info.id) {
      return &info;
    }
  }
  return nullptr;
}

std::vector<Binding> DefaultBindings() {
  const auto bind = [](ui::KeyboardCode key, int modifiers, Action action) {
    return Binding{ui::Accelerator(key, modifiers), action};
  };
  return {
      // Zen: compact mode.
      bind(ui::VKEY_S, kCtrl, Action::kToggleCompactMode),
      bind(ui::VKEY_S, kCtrl | kAlt, Action::kCompactModeShowSidebar),
      // Zen moves Save Page off Ctrl+S.
      bind(ui::VKEY_S, kCtrl | kAlt | kShift, Action::kSavePage),
      // Zen: workspaces.
      bind(ui::VKEY_RIGHT, kCtrl | kAlt, Action::kWorkspaceForward),
      bind(ui::VKEY_LEFT, kCtrl | kAlt, Action::kWorkspaceBackward),
      // Zen: split view.
      bind(ui::VKEY_G, kCtrl | kAlt, Action::kSplitGrid),
      bind(ui::VKEY_V, kCtrl | kAlt, Action::kSplitVertical),
      bind(ui::VKEY_H, kCtrl | kAlt, Action::kSplitHorizontal),
      bind(ui::VKEY_U, kCtrl | kAlt, Action::kUnsplit),
      // Ctrl+Shift+* : "*" is Shift+8 on most layouts, or the keypad key.
      bind(ui::VKEY_8, kCtrl | kShift, Action::kNewEmptySplit),
      bind(ui::VKEY_MULTIPLY, kCtrl | kShift, Action::kNewEmptySplit),
      // Zen: tabs and pages.
      bind(ui::VKEY_C, kCtrl | kShift, Action::kCopyUrl),
      bind(ui::VKEY_C, kCtrl | kShift | kAlt, Action::kCopyUrlMarkdown),
      bind(ui::VKEY_D, kCtrl | kShift, Action::kTogglePinTab),
      bind(ui::VKEY_K, kCtrl | kShift, Action::kCloseUnpinnedTabs),
      bind(ui::VKEY_O, kCtrl, Action::kGlanceExpand),
      bind(ui::VKEY_L, kAlt | kShift, Action::kLibrary),
      // Zen moves the inspector off Ctrl+Shift+C.
      bind(ui::VKEY_L, kCtrl | kShift, Action::kInspect),
      // Zen (Firefox) windows: Ctrl+Shift+N new window, Ctrl+Shift+P private.
      bind(ui::VKEY_N, kCtrl | kShift, Action::kNewWindow),
      bind(ui::VKEY_P, kCtrl | kShift, Action::kNewPrivateWindow),
      // Zen inherits these from Firefox; Chrome leaves the keys free.
      bind(ui::VKEY_M, kCtrl, Action::kMuteSite),
      bind(ui::VKEY_B, kCtrl, Action::kBookmarks),
      bind(ui::VKEY_Y, kCtrl | kShift, Action::kDownloads),
      bind(ui::VKEY_R, kCtrl | kAlt, Action::kReaderMode),
      // Koan-only.
      bind(ui::VKEY_B, kCtrl | kAlt, Action::kToggleSingleToolbar),
      bind(ui::VKEY_N, kCtrl | kAlt, Action::kNewWorkspace),
      // Ctrl+, : Zen's "Open Preferences" (Cmd+, on macOS).
      bind(ui::VKEY_OEM_COMMA, kCtrl, Action::kOpenSettings),
  };
}

std::vector<Binding> Bindings(const base::DictValue& overrides) {
  std::vector<Binding> bindings;
  const auto is_bound = [&bindings](const ui::Accelerator& accelerator) {
    return std::ranges::any_of(bindings, [&](const Binding& binding) {
      return binding.accelerator == accelerator;
    });
  };
  std::set<Action> custom;
  for (const auto [id, value] : overrides) {
    const Info* info = FindAction(id);
    if (!info || !value.is_dict()) {
      continue;
    }
    // Key 0: the user cleared the shortcut.
    if (value.GetDict().FindInt(kKey) == 0) {
      custom.insert(info->action);
      continue;
    }
    const ui::Accelerator accelerator = FromPrefValue(value.GetDict());
    if (accelerator.key_code() == ui::VKEY_UNKNOWN || is_bound(accelerator)) {
      continue;
    }
    custom.insert(info->action);
    bindings.push_back({accelerator, info->action});
  }
  for (const Binding& binding : DefaultBindings()) {
    if (!custom.contains(binding.action) && !is_bound(binding.accelerator)) {
      bindings.push_back(binding);
    }
  }
  return bindings;
}

base::span<const CycleKey> CycleKeys() {
  static constexpr CycleKey kCycleKeys[] = {
      {ui::VKEY_TAB, kCtrl, true},
      {ui::VKEY_TAB, kCtrl | kShift, false},
      {ui::VKEY_NEXT, kCtrl, true},
      {ui::VKEY_PRIOR, kCtrl, false},
  };
  return kCycleKeys;
}

bool IsValidCustomAccelerator(const ui::Accelerator& accelerator) {
  const ui::KeyboardCode key = accelerator.key_code();
  if (key == ui::VKEY_UNKNOWN || key > 0xFF ||
      (accelerator.modifiers() & ~kModifiers)) {
    return false;
  }
  for (const CycleKey& cycle_key : CycleKeys()) {
    if (accelerator == ui::Accelerator(cycle_key.key, cycle_key.modifiers)) {
      return false;
    }
  }
  switch (key) {
    case ui::VKEY_SHIFT:
    case ui::VKEY_CONTROL:
    case ui::VKEY_MENU:
    case ui::VKEY_LWIN:
    case ui::VKEY_RWIN:
      return false;
    default:
      break;
  }
  if (key >= ui::VKEY_F1 && key <= ui::VKEY_F24) {
    return true;
  }
  return (accelerator.modifiers() & (kCtrl | kAlt | ui::EF_COMMAND_DOWN)) != 0;
}

base::DictValue ToPrefValue(const ui::Accelerator& accelerator) {
  return base::DictValue()
      .Set(kKey, static_cast<int>(accelerator.key_code()))
      .Set(kModifiersKey, accelerator.modifiers());
}

ui::Accelerator FromPrefValue(const base::DictValue& value) {
  const int key = value.FindInt(kKey).value_or(0);
  if (key <= 0 || key > 0xFF) {
    return ui::Accelerator();
  }
  const ui::Accelerator accelerator(static_cast<ui::KeyboardCode>(key),
                                    value.FindInt(kModifiersKey).value_or(0));
  return IsValidCustomAccelerator(accelerator) ? accelerator
                                               : ui::Accelerator();
}

}  // namespace koan::shortcuts

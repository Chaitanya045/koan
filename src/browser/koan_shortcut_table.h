// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_SHORTCUT_TABLE_H_
#define KOAN_BROWSER_KOAN_SHORTCUT_TABLE_H_

#include <vector>

#include "base/containers/span.h"
#include "base/values.h"
#include "ui/base/accelerators/accelerator.h"

// Koan's keyboard shortcuts as data: every action with its Zen group and
// label, the default keys, and the user's own keys from the settings page
// (prefs::kShortcuts). Shared by the per-window KoanShortcutController,
// which runs them, and chrome://koan-settings, which edits them.
namespace koan::shortcuts {

enum class Action {
  // Compact mode / layout.
  kToggleCompactMode,
  kCompactModeShowSidebar,
  kToggleSingleToolbar,
  // Workspaces.
  kWorkspaceForward,
  kWorkspaceBackward,
  kNewWorkspace,
  // Split view.
  kSplitGrid,
  kSplitVertical,
  kSplitHorizontal,
  kUnsplit,
  kNewEmptySplit,
  // Tabs and pages.
  kCopyUrl,
  kCopyUrlMarkdown,
  kTogglePinTab,
  kCloseUnpinnedTabs,
  kGlanceExpand,
  kSavePage,
  kLibrary,
  kInspect,
  kNewWindow,
  kNewPrivateWindow,
  kMuteSite,
  kBookmarks,
  kDownloads,
  kReaderMode,
  kOpenSettings,
};

// A group of the shortcut editor, in Zen's order (VALID_SHORTCUT_GROUPS in
// ZenKeyboardShortcuts.mjs) with Zen's titles.
struct Group {
  const char* id;
  const char* title;
};

struct Info {
  Action action;
  // Stable id, the key in prefs::kShortcuts.
  const char* id;
  // Group::id.
  const char* group;
  // Zen's label where Zen has the shortcut (zen-preferences.ftl).
  const char* label;
};

struct Binding {
  ui::Accelerator accelerator;
  Action action;
};

base::span<const Group> Groups();

// Every action, grouped in Groups() order.
base::span<const Info> Actions();

// Returns null for an unknown id.
const Info* FindAction(std::string_view id);

// Zen's defaults (ZenKeyboardShortcuts.mjs, shortcuts version 21, Linux) plus
// Koan's own, in docs/USAGE.md order. An action may have several keys.
std::vector<Binding> DefaultBindings();

// The bindings in effect: the defaults, with each action the user rebound in
// `overrides` (prefs::kShortcuts) replaced by its one custom key, or by none
// if the user cleared it.
std::vector<Binding> Bindings(const base::DictValue& overrides);

// Whether `accelerator` may be used for a custom shortcut: a key with Ctrl,
// Alt or Meta (Shift alone would take a key away from typing), or a function
// key.
bool IsValidCustomAccelerator(const ui::Accelerator& accelerator);

// The pref value for an action's custom key; `accelerator` is empty
// (VKEY_UNKNOWN) for "not set".
base::DictValue ToPrefValue(const ui::Accelerator& accelerator);
ui::Accelerator FromPrefValue(const base::DictValue& value);

}  // namespace koan::shortcuts

#endif  // KOAN_BROWSER_KOAN_SHORTCUT_TABLE_H_

// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_settings_rows.h"

#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"

namespace koan::settings {

namespace {

constexpr Pane kPanes[] = {
    {"looks", "Look and Feel"},
    {"tabs", "Tab Management"},
    {kShortcutsPane, "Keyboard Shortcuts"},
};

// Zen's groups (zen-preferences.ftl), in Zen's order.
constexpr Group kGroups[] = {
    {"layout", "looks", "Sidebar and tabs layout", "Browser Layout",
     "Choose the layout that suits you best"},
    {"compact", "looks", "Theme Settings", "Show in compact view",
     "Only show the toolbars you use!"},
    {"dark-theme", "looks", "Theme Settings", "Dark Theme Styles",
     "Customize the dark theme to your liking"},
    {"window-drag", "looks", "Theme Settings", "Window dragging",
     "Move the window by dragging empty space at the top of websites, just "
     "like the titlebar."},
    {"glance", "looks", "Glance", "General settings for glance",
     "Get a quick overview of your links without opening them in a new tab"},
    {"urlbar", "looks", "URL Bar", "General settings for the URL bar",
     "Customize the URL bar to your liking"},
    {"workspaces", "tabs", "Workspaces", "General settings for workspaces",
     "With workspaces, you can have multiple browsing sessions at once!"},
    {"pinned-tabs", "tabs", "Pinned Tabs", "General settings for pinned tabs",
     "Manage additional behavior of pinned tabs"},
};

constexpr Option kLayouts[] = {
    {"true", "Only Sidebar", "single"},
    {"false", "Sidebar and Top Toolbar", "multiple"},
};

constexpr Row kRows[] = {
    // Look and Feel.
    {.group = "layout",
     .control = Control::kLayoutPicker,
     .pref = prefs::kSingleToolbarEnabled,
     .label = "Browser Layout",
     .options = kLayouts,
     .is_available = features::IsCompactModeEnabled},
    {.group = "compact",
     .control = Control::kCheckbox,
     .pref = prefs::kCompactModeEnabled,
     .label = "Enable Koan's compact mode",
     .is_available = features::IsCompactModeEnabled},
    // Tab Management.
    {.group = "workspaces",
     .control = Control::kCheckbox,
     .pref = prefs::kWorkspaceTint,
     .label = "Tint the browser with the active workspace's colour",
     .is_available = features::IsWorkspacesEnabled},
};

}  // namespace

base::span<const Pane> Panes() {
  return kPanes;
}

base::span<const Group> Groups() {
  return kGroups;
}

base::span<const Row> Rows() {
  return kRows;
}

const Row* FindRow(std::string_view pref) {
  for (const Row& row : kRows) {
    if (pref == row.pref) {
      return &row;
    }
  }
  return nullptr;
}

}  // namespace koan::settings

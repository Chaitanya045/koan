// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_settings_rows.h"

#include "base/feature_list.h"
#include "build/build_config.h"
#include "components/performance_manager/public/features.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
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
    {"vertical-tabs", "looks", "Sidebar and tabs layout", "Vertical Tabs",
     "Manage your tabs in a vertical layout"},
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
    // Koan's own: Zen has no setting for this.
    {"background-tabs", "tabs", "Performance", "Background tabs",
     "Save memory and battery with the tabs you aren't using. Memory Saver "
     "and Energy Saver are in Chromium's settings (Performance)."},
};

// Chromium's "Ctrl+Tab in recently used order" (prefs::kCtrlTabMru,
// chrome://settings > Appearance) takes Ctrl+Tab over Koan's settings.
constexpr char kCtrlTabMru[] = "browser.ctrl_tab_mru";
constexpr char kCtrlTabMruNote[] =
    "Ctrl+Tab will cycle by recently used order, as it is enabled in "
    "Chromium's settings (Appearance); Ctrl+Page Up/Down still follow this "
    "setting";

// The infinite-tabs freezing that the freeze switch controls (on in Koan,
// chrome://flags/#koan-freeze-background-tabs).
bool IsTabFreezingAvailable() {
  return base::FeatureList::IsEnabled(
      performance_manager::features::kInfiniteTabsFreezing);
}

constexpr Option kLayouts[] = {
    {"true", "Only Sidebar", "single"},
    {"false", "Sidebar and Top Toolbar", "multiple"},
};

// What compact mode hides (zen-preferences.ftl zen-compact-mode-styles-*).
constexpr Option kCompactModeHides[] = {
    {prefs::kCompactModeHideSidebar, "Hide Tab bar"},
    {prefs::kCompactModeHideToolbar, "Hide Top bar"},
    {prefs::kCompactModeHideBoth, "Hide Both"},
};

// Zen's Glance trigger methods (zen-glance-trigger-*), in Zen's order. On
// macOS Ctrl+click is a right-click, so it can't trigger anything.
constexpr Option kGlanceTriggers[] = {
#if !BUILDFLAG(IS_MAC)
    {"ctrl", "Ctrl + Click"},
#endif
    {"alt", "Alt + Click"},
    {"shift", "Shift + Click"},
    {"meta", "Meta (Command) + Click"},
};

// Zen's zen.urlbar.behavior choices.
constexpr Option kUrlBarBehaviors[] = {
    {prefs::kUrlBarBehaviorNormal, "Normal"},
    {prefs::kUrlBarBehaviorFloatingOnType, "Floating only when typing"},
    {prefs::kUrlBarBehaviorFloat, "Always floating"},
};

// Zen's close-shortcut behaviours for pinned tabs, in Zen's order.
constexpr Option kPinnedTabCloseBehaviors[] = {
    {"reset-unload-switch", "Reset URL, unload and switch to next tab"},
    {"unload-switch", "Unload and switch to next tab"},
    {"reset-switch", "Reset URL and switch to next tab"},
    {"switch", "Switch to next tab"},
    {"reset", "Reset URL"},
    {"close", "Close tab"},
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
    {.group = "compact",
     .control = Control::kMenu,
     .pref = prefs::kCompactModeHide,
     .label = "What compact mode hides",
     .options = kCompactModeHides,
     .is_available = features::IsCompactModeEnabled,
     .indent = true,
     .note = "With the Only Sidebar layout there is no top bar: compact mode "
             "hides the sidebar"},
    {.group = "compact",
     .control = Control::kCheckbox,
     .pref = prefs::kCompactModeToolbarFlashPopup,
     .label = "Briefly make the toolbar popup when switching or opening new "
              "tabs in compact mode",
     .is_available = features::IsCompactModeEnabled,
     .indent = true},
    {.group = "vertical-tabs",
     .control = Control::kCheckbox,
     .pref = prefs::kTabsOnRight,
     .label = "Tabs on the right",
     .is_available = features::IsSidebarEnabled},
    {.group = "window-drag",
     .control = Control::kCheckbox,
     .pref = prefs::kDragWindowFromContent,
     .label = "Allow dragging the window from web pages",
     .is_available = features::IsWindowDragEnabled},
    {.group = "glance",
     .control = Control::kCheckbox,
     .pref = prefs::kGlanceEnabled,
     .label = "Enable Glance",
     .is_available = features::IsGlanceEnabled},
    {.group = "glance",
     .control = Control::kMenu,
     .pref = prefs::kGlanceActivationMethod,
     .label = "Trigger method",
     .options = kGlanceTriggers,
     .depends_on = prefs::kGlanceEnabled,
     .is_available = features::IsGlanceEnabled,
     // Blink reads it when a page's renderer process starts.
     .note = "Applies to pages opened after the change; reload others in a "
             "new tab or restart Koan"},
    // Zen shows only the behaviour; the other two are its about:config
    // prefs zen.urlbar.replace-newtab and
    // zen.urlbar.single-toolbar-show-copy-url.
    {.group = "urlbar",
     .control = Control::kMenu,
     .pref = prefs::kUrlBarBehavior,
     .label = "Behavior",
     .options = kUrlBarBehaviors,
     .is_available = features::IsUrlBarEnabled},
    {.group = "urlbar",
     .control = Control::kCheckbox,
     .pref = prefs::kUrlBarReplaceNewTab,
#if BUILDFLAG(IS_MAC)
     .label = "Open the URL bar instead of a new tab page (\u2318T, New Tab)",
#else
     .label = "Open the URL bar instead of a new tab page (Ctrl+T, New Tab)",
#endif
     .is_available = features::IsUrlBarEnabled},
    {.group = "urlbar",
     .control = Control::kCheckbox,
     .pref = prefs::kUrlBarShowCopyUrl,
     .label = "Show the copy URL button in the sidebar's URL bar",
     .is_available = features::IsUrlBarEnabled},
    // Tab Management.
    {.group = "workspaces",
     .control = Control::kCheckbox,
     .pref = prefs::kWorkspaceTint,
     .label = "Tint the browser with the active workspace's colour",
     .is_available = features::IsWorkspacesEnabled},
    // Zen keeps its tab behaviours in the workspaces group too.
    {.group = "workspaces",
     .control = Control::kCheckbox,
     .pref = prefs::kCloseOnBackWithNoHistory,
     .label = "Close tab and switch to its owner tab (or most recently used "
              "tab) when going back with no history"},
    {.group = "workspaces",
     .control = Control::kCheckbox,
     .pref = prefs::kCtrlTabSkipPending,
     .label = "Ignore Pending tabs when cycling with Ctrl+Tab",
     .note = kCtrlTabMruNote,
     .note_pref = kCtrlTabMru},
    {.group = "workspaces",
     .control = Control::kCheckbox,
     .pref = prefs::kCtrlTabWithinKind,
     .label = "Ctrl+Tab cycles within Essential or Workspace tabs only",
     .note = kCtrlTabMruNote,
     .note_pref = kCtrlTabMru},
    {.group = "workspaces",
     .control = Control::kCheckbox,
     .pref = prefs::kSelectRecentlyUsedOnClose,
     .label = "When closing a tab, switch to the most recently used tab "
              "instead of the next tab"},
    {.group = "pinned-tabs",
     .control = Control::kCheckbox,
     .pref = prefs::kPinnedTabRestoreToPinnedUrl,
     .label = "Restore pinned tabs to their originally pinned URL on startup",
     .is_available = features::IsPinnedTabsEnabled},
    {.group = "pinned-tabs",
     .control = Control::kMenu,
     .pref = prefs::kPinnedTabCloseShortcutBehavior,
     .label = "Close Tab Shortcut Behavior",
     .options = kPinnedTabCloseBehaviors,
     .is_available = features::IsPinnedTabsEnabled},
    {.group = "background-tabs",
     .control = Control::kCheckbox,
     .pref = performance_manager::user_tuning::prefs::kTabFreezingEnabled,
     .label = "Freeze background tabs you haven't used recently (they wake "
              "for a few seconds each minute; pinned tabs and tabs that "
              "played audio, are in a call or show notifications keep "
              "running)",
     .is_available = IsTabFreezingAvailable,
     .local_state = true},
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

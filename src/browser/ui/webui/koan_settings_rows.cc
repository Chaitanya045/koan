// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   zen-preferences.ftl
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/webui/koan_settings_rows.h"

#include "base/feature_list.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/pref_names.h"
#include "components/performance_manager/public/features.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_window_glass.h"

namespace koan::settings {

namespace {

constexpr Pane kPanes[] = {
    {"looks", "Look and Feel"},
    {"tabs", "Tab Management"},
    {kShortcutsPane, "Keyboard Shortcuts"},
    // Koan's own: Zen has no agent control.
    {"agents", "Agent Control"},
};

// Zen's groups (zen-preferences.ftl), in Zen's order.
constexpr Group kGroups[] = {
    // Koan's own (Zen has no such setting): the shadcn look (#48).
    {"look", "looks", "Appearance", "Style",
     "Koan's default look is Zen's. shadcn/ui's design system (dark, with "
     "Geist) is available too. Changes apply at once; the font changes "
     "after a restart."},
    {"look-colors", "looks", "Appearance", "shadcn colours",
     "For the shadcn style: light or dark, a base colour, an accent colour, "
     "or a theme of your own from ui.shadcn.com/themes or tweakcn.com."},
    // Koan's own: Zen has no bookmarks bar.
    {"bookmarks-bar", "looks", "Appearance", "Bookmarks bar",
     "Zen has no bookmarks bar, so Koan shows none, also on the new tab page "
     "and after importing bookmarks. Turn it on to see Chromium's."},
    {"layout", "looks", "Sidebar and tabs layout", "Browser Layout",
     "Choose the layout that suits you best"},
    {"vertical-tabs", "looks", "Sidebar and tabs layout", "Vertical Tabs",
     "Manage your tabs in a vertical layout"},
    {"compact", "looks", "Theme Settings", "Show in compact view",
     "Only show the toolbars you use!"},
    {"dark-theme", "looks", "Theme Settings", "Dark Theme Styles",
     "Customize the dark theme to your liking"},
    {"window-material", "looks", "Theme Settings", "Window material",
     "Let the desktop show through the window, blurred"},
    {"window-drag", "looks", "Theme Settings", "Window dragging",
     "Move the window by dragging empty space at the top of websites, just "
     "like the titlebar."},
    {"glance", "looks", "Glance", "General settings for glance",
     "Get a quick overview of your links without opening them in a new tab"},
    {"urlbar", "looks", "URL Bar", "General settings for the URL bar",
     "Customize the URL bar to your liking"},
    // Koan's own: Zen's extracted source has no setting for this (Firefox's
    // about:preferences#home has one).
    {"newtab", "looks", "New Tab Page", "New tabs",
     "Choose what a new tab page shows"},
    {"workspaces", "tabs", "Workspaces", "General settings for workspaces",
     "With workspaces, you can have multiple browsing sessions at once!"},
    {"pinned-tabs", "tabs", "Pinned Tabs", "General settings for pinned tabs",
     "Manage additional behavior of pinned tabs"},
    // Koan's own: Zen has no setting for this.
    {"background-tabs", "tabs", "Performance", "Background tabs",
     "Save memory and battery with the tabs you aren't using. Memory Saver "
     "and Energy Saver are in Chromium's settings (Performance)."},
    // Koan's own (.agent/notes/agent-control.md section 6.1).
    {"agent-control", "agents", "Agents", "Agent ready",
     "Lets agents you approve open and work in background tabs. Tabs stay "
     "awake only while an agent is using them. Every new program asks you "
     "first, naming the program. Agent tabs are in containers of their own "
     "and never get your logins or cookies. "
     "Turned off by your organisation's policy if remote debugging is."},
    {"agent-limits", "agents", "Agents", "Limits",
     "A change applies to new connections and new awake tabs; nothing running "
     "is cut off."},
};

// Enterprise policy RemoteDebuggingAllowed=false turns agent control off; its
// rows are then hidden (the group says why).
bool IsAgentControlAllowed() {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  return local_state &&
         local_state->GetBoolean(::prefs::kDevToolsRemoteDebuggingAllowed);
}

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

// Like Firefox's "New tabs" choices: a blank page, which is what Zen shows,
// or the browser's own page (here Chromium's).
constexpr Option kNewTabPages[] = {
    {prefs::kNewTabPageKoan, "Blank page"},
    {prefs::kNewTabPageChromium, "Chromium's new tab page"},
};
constexpr char kNewTabPageNote[] =
    "A new tab page set by your administrator or by an extension is shown "
    "instead.";

// Zen's close-shortcut behaviours for pinned tabs, in Zen's order.
constexpr Option kPinnedTabCloseBehaviors[] = {
    {"reset-unload-switch", "Reset URL, unload and switch to next tab"},
    {"unload-switch", "Unload and switch to next tab"},
    {"reset-switch", "Reset URL and switch to next tab"},
    {"switch", "Switch to next tab"},
    {"reset", "Reset URL"},
    {"close", "Close tab"},
};

// Agent control's limits (koan/browser/agent/koan_agent_limits.h). Only these
// values can be set, so 0 and "unlimited" cannot. Above 8 awake tabs the
// option says what it costs (T-limits-5).
constexpr Option kAgentLeasedTabs[] = {
    {"1", "1"},
    {"2", "2"},
    {"3", "3"},
    {"4", "4 (default)"},
    {"5", "5"},
    {"6", "6"},
    {"7", "7"},
    {"8", "8"},
    {"9", "9 (each awake tab uses its own memory and power)"},
    {"10", "10 (each awake tab uses its own memory and power)"},
    {"11", "11 (each awake tab uses its own memory and power)"},
    {"12", "12 (each awake tab uses its own memory and power)"},
    {"13", "13 (each awake tab uses its own memory and power)"},
    {"14", "14 (each awake tab uses its own memory and power)"},
    {"15", "15 (each awake tab uses its own memory and power)"},
    {"16", "16 (each awake tab uses its own memory and power)"},
};
constexpr Option kAgentPrompts[] = {
    {"1", "1"}, {"2", "2"}, {"3", "3 (default)"}, {"4", "4"}, {"5", "5"},
    {"6", "6"}, {"7", "7"}, {"8", "8"},           {"9", "9"}, {"10", "10"},
};
constexpr Option kAgentFailures[] = {
    {"1", "1"},           {"2", "2"},   {"3", "3"},   {"4", "4"},
    {"5", "5 (default)"}, {"6", "6"},   {"7", "7"},   {"8", "8"},
    {"9", "9"},           {"10", "10"}, {"11", "11"}, {"12", "12"},
    {"13", "13"},         {"14", "14"}, {"15", "15"}, {"16", "16"},
    {"17", "17"},         {"18", "18"}, {"19", "19"}, {"20", "20"},
};

// The shadcn look (koan::look names).
constexpr Option kLookStyles[] = {
    {"zen", "Zen (default)"},
    {"shadcn", "shadcn/ui"},
};
constexpr Option kLookModes[] = {
    {"dark", "Dark"},
    {"light", "Light"},
    {"auto", "Automatic (follow the system)"},
};
constexpr Option kLookBases[] = {
    {"black", "Black (default)"}, {"neutral", "Neutral"}, {"zinc", "Zinc"},
    {"slate", "Slate"},           {"stone", "Stone"},     {"gray", "Gray"},
};
constexpr Option kLookAccents[] = {
    {"none", "None (the base colour's own)"},
    {"blue", "Blue"},
    {"green", "Green"},
    {"orange", "Orange"},
    {"red", "Red"},
    {"rose", "Rose"},
    {"violet", "Violet"},
    {"yellow", "Yellow"},
};

constexpr Row kRows[] = {
    // Look and Feel.
    {.group = "look",
     .control = Control::kMenu,
     .pref = prefs::kLookStyle,
     .label = "Look",
     .options = kLookStyles,
     .local_state = true},
    {.group = "look-colors",
     .control = Control::kMenu,
     .pref = prefs::kLookMode,
     .label = "Mode",
     .options = kLookModes,
     .local_state = true},
    {.group = "look-colors",
     .control = Control::kMenu,
     .pref = prefs::kLookBase,
     .label = "Base colour",
     .options = kLookBases,
     .local_state = true},
    {.group = "look-colors",
     .control = Control::kMenu,
     .pref = prefs::kLookAccent,
     .label = "Accent colour",
     .options = kLookAccents,
     .local_state = true},
    {.group = "look-colors",
     .control = Control::kTextArea,
     .pref = prefs::kLookCustomCss,
     .label =
         "Paste a theme (:root and .dark variables, as ui.shadcn.com/themes "
         "or tweakcn.com make them)",
     .local_state = true},
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
    {.group = "bookmarks-bar",
     .control = Control::kCheckbox,
     .pref = prefs::kBookmarksBarShow,
     .label = "Show the bookmarks bar"},
    {.group = "vertical-tabs",
     .control = Control::kCheckbox,
     .pref = prefs::kTabsOnRight,
     .label = "Tabs on the right",
     .is_available = features::IsSidebarEnabled},
    // Zen's zen.widget.macos.window-vibrancy (default on), here Chromium's
    // pref; macOS's Reduce Transparency turns it off as well.
    {.group = "window-material",
     .control = Control::kCheckbox,
     .pref = ::prefs::kGlassFrameEnabled,
     .label = "Translucent window",
     .is_available = IsWindowGlassSupported,
     .local_state = true,
     .note = "Off while macOS's Reduce Transparency is on"},
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
    // Zen's about:config prefs zen.glance.open-essential-external-links and
    // zen.glance.enable-contextmenu-search (both on).
    {.group = "glance",
     .control = Control::kCheckbox,
     .pref = prefs::kGlanceOpenPinnedExternalLinks,
     .label = "Open links from Essentials and pinned tabs to other sites in "
              "Glance",
     .depends_on = prefs::kGlanceEnabled,
     .is_available = features::IsGlanceEnabled},
    {.group = "glance",
     .control = Control::kCheckbox,
     .pref = prefs::kGlanceContextMenuSearch,
     .label = "Open \"Search for\" from the context menu in Glance",
     .depends_on = prefs::kGlanceEnabled,
     .is_available = features::IsGlanceEnabled},
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
    // Zen's about:config pref zen.theme.acrylic-urlbar (default off). Off
    // macOS it blurs the page only (there is no desktop to see through).
    {.group = "urlbar",
     .control = Control::kCheckbox,
     .pref = prefs::kLookAcrylicUrlBar,
     .label = "Glass URL bar (translucent, with the page blurred behind it)",
     .is_available = features::IsUrlBarEnabled,
     .local_state = true,
     .note = "Off while macOS's Reduce Transparency is on, and in a window "
             "that isn't active"},
    {.group = "newtab",
     .control = Control::kMenu,
     .pref = prefs::kNewTabPage,
     .label = "New tab page",
     .options = kNewTabPages,
     .is_available = features::IsNewTabEnabled,
     .note = kNewTabPageNote},
    // Tab Management.
    {.group = "workspaces",
     .control = Control::kCheckbox,
     .pref = prefs::kWorkspaceTint,
     .label = "Tint the browser with the active Space's colour",
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
    // Zen's about:config pref zen.tabs.open-pinned-in-new-tab (on).
    {.group = "pinned-tabs",
     .control = Control::kCheckbox,
     .pref = prefs::kOpenPinnedInNewTab,
     .label = "Open other sites from a pinned tab's URL bar in a new tab"},
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
    // Agents.
    {.group = "agent-control",
     .control = Control::kCheckbox,
     .pref = prefs::kAgentControlEnabled,
     .label = "Agent ready",
     .is_available = IsAgentControlAllowed,
     .local_state = true},
    {.group = "agent-limits",
     .control = Control::kMenu,
     .pref = prefs::kAgentControlMaxLeasedTabs,
     .label = "Awake agent tabs at a time",
     .options = kAgentLeasedTabs,
     .is_available = IsAgentControlAllowed,
     .local_state = true},
    {.group = "agent-limits",
     .control = Control::kCheckbox,
     .pref = prefs::kAgentControlLowPowerHalves,
     .label = "Fewer awake tabs in Low Power Mode on battery (half, at least "
              "one)",
     .is_available = IsAgentControlAllowed,
     .indent = true,
     .local_state = true},
    {.group = "agent-limits",
     .control = Control::kMenu,
     .pref = prefs::kAgentControlMaxPromptsPerMinute,
     .label = "Approval requests per minute",
     .options = kAgentPrompts,
     .is_available = IsAgentControlAllowed,
     .local_state = true},
    {.group = "agent-limits",
     .control = Control::kMenu,
     .pref = prefs::kAgentControlMaxAuthFailures,
     .label = "Failed connection attempts before a 5-minute block",
     .options = kAgentFailures,
     .is_available = IsAgentControlAllowed,
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

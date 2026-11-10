// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_PREFS_H_
#define KOAN_BROWSER_KOAN_PREFS_H_

class PrefRegistrySimple;

namespace user_prefs {
class PrefRegistrySyncable;
}

// Koan's prefs belong to the regular profile. Incognito windows read and
// write them through Profile::GetOriginalProfile()->GetPrefs(), never their
// own PrefService: a write there would land in incognito's in-memory layer
// and hide later changes, so the window would drift from the others and from
// chrome://koan-settings.
namespace koan::prefs {

// The expanded sidebar's default width, and where a double-click on its edge
// puts it back: Zen's kDefaultSidebarWidth on macOS (ZenCustomizableUI).
// Chromium's is tabs::kVerticalTabStripDefaultUncollapsedWidth, 240.
inline constexpr int kSidebarDefaultWidth = 230;

// Koan's look (issue #48), local-state prefs because colours are mixed
// per colour-provider key, which knows no profile. The style is shadcn/ui's
// design system ("shadcn") or Zen's ("zen", the default); the mode, base
// colour, accent and pasted theme apply to the shadcn style only (the Zen
// style follows the system's light or dark mode). Names are
// koan::look::StyleToString() & co.
// - kLookMode: "auto" (the system), "light" or "dark" (the default).
// - kLookBase: black (default: Koan's near-black Zinc), or shadcn's base
//   colours neutral, zinc, slate, stone, gray.
// - kLookAccent: none (default), blue, green, orange, red, rose, violet,
//   yellow.
// - kLookCustomCss: a theme pasted from ui.shadcn.com/themes or tweakcn.com,
//   applied over the base colour and accent; empty for none.
inline constexpr char kLookStyle[] = "koan.look.style";
inline constexpr char kLookMode[] = "koan.look.mode";
inline constexpr char kLookBase[] = "koan.look.base";
inline constexpr char kLookAccent[] = "koan.look.accent";
inline constexpr char kLookCustomCss[] = "koan.look.custom_css";
// Zen's zen.theme.acrylic-urlbar (default off): the floating URL bar is a
// glass pane, translucent with the page blurred behind it. Both styles.
inline constexpr char kLookAcrylicUrlBar[] = "koan.look.acrylic_urlbar";

// Zen's zen.haptic-feedback.enabled (default on): the trackpad ticks as a
// dragged tab or Space button moves a slot and as the theme picker's sliders
// pass a step (koan::PlayHapticFeedback()). macOS only; a local-state pref, as
// Zen's is the application's.
inline constexpr char kHapticFeedback[] = "koan.haptic_feedback";

// Whether compact mode (auto-hiding sidebar and toolbar) is on. Applies to
// every window of the profile.
inline constexpr char kCompactModeEnabled[] = "koan.compact_mode.enabled";

// What compact mode hides (Zen's "Compact Mode" submenu and settings,
// `zen.view.compact.hide-tabbar` / `hide-toolbar`): one of the values below,
// by default the sidebar, like Zen. With single toolbar there is no top
// toolbar, so compact mode hides just the sidebar.
inline constexpr char kCompactModeHide[] = "koan.compact_mode.hide";
inline constexpr char kCompactModeHideSidebar[] = "sidebar";
inline constexpr char kCompactModeHideToolbar[] = "toolbar";
inline constexpr char kCompactModeHideBoth[] = "both";

// Zen's "Briefly make the toolbar popup when switching or opening new tabs in
// compact mode" (`zen.view.compact.toolbar-flash-popup`): the hidden sidebar
// pops up for 800 ms when the active tab changes. Off by default, like Zen.
inline constexpr char kCompactModeToolbarFlashPopup[] =
    "koan.compact_mode.toolbar_flash_popup";

// Zen's "Tabs on the right" (`zen.tabs.vertical.right-side`): the sidebar is
// on the right of the window, everything mirrored.
inline constexpr char kTabsOnRight[] = "koan.tabs.on_right";

// Zen's "Show New Tab Button on Tab List" (`zen.tabs.show-newtab-vertical`):
// the "+ New Tab" row above the unpinned tabs. On by default, like Zen.
inline constexpr char kShowNewTabVertical[] = "koan.tabs.show_newtab_vertical";

// Zen's "Move the new tab button to the top"
// (`zen.view.show-newtab-button-top`): the "+ New Tab" row sits above a Space's
// unpinned tabs and new tabs open at the top of the Space. Off: the row sits
// after the last tab and new tabs open at the end of the Space. On by default,
// like Zen.
inline constexpr char kShowNewTabButtonTop[] =
    "koan.view.show_newtab_button_top";

// Whether the "Sidebar hidden!" toast was shown, after a drag of the
// sidebar's edge hid the sidebar (`zen.view.compact.drag-collapse-hint-seen`).
// Zen shows it once.
inline constexpr char kCompactModeDragCollapseHintSeen[] =
    "koan.compact_mode.drag_collapse_hint_seen";

// Zen's single-toolbar layout: no top toolbar; navigation and a URL pill at
// the top of the sidebar. On by default, like Zen.
inline constexpr char kSingleToolbarEnabled[] = "koan.single_toolbar";

// How the URL bar opens when the address is edited (Zen's
// zen.urlbar.behavior): "normal" edits it in place (in the sidebar's URL
// pill), "floating-on-type" (Zen's default) floats over the page when the bar
// is opened with the keyboard (Cmd+L, Cmd+T) and edits in the pill after a
// click on it (typing then never floats it), "float" floats over the page
// right away.
inline constexpr char kUrlBarBehavior[] = "koan.urlbar.behavior";
inline constexpr char kUrlBarBehaviorNormal[] = "normal";
inline constexpr char kUrlBarBehaviorFloatingOnType[] = "floating-on-type";
inline constexpr char kUrlBarBehaviorFloat[] = "float";

// Zen's zen.urlbar.replace-newtab: Ctrl+T and the sidebar's new-tab button
// open the URL bar instead of a tab; a tab opens only with what the user
// opens from it. On by default, like Zen.
inline constexpr char kUrlBarReplaceNewTab[] = "koan.urlbar.replace_newtab";

// Zen's zen.urlbar.single-toolbar-show-copy-url: a copy-URL button in the
// sidebar's URL pill on hover. On by default, like Zen.
inline constexpr char kUrlBarShowCopyUrl[] = "koan.urlbar.show_copy_url";

// Whether windows show their active Space's theme (Zen's per-Space
// gradient themes).
inline constexpr char kWorkspaceTint[] = "koan.workspaces.tint";

// Zen's dark theme styles: 0 default, 1 night (AMOLED), 2 colorful.
inline constexpr char kDarkThemeStyle[] = "koan.theme.dark_style";

// Whether the theme picker shows the custom colour list
// (zen.theme.gradient.show-custom-colors, off by default like Zen's).
inline constexpr char kShowCustomColors[] =
    "koan.theme.gradient.show_custom_colors";

// How the user moves between Spaces (Zen's about:config prefs, with Zen's
// defaults):
// - kWorkspacesWrapAround: past the last Space comes the first, and the
//   other way (zen.workspaces.wrap-around-navigation, on).
// - kWorkspacesNaturalScroll: the wheel and the touchpad swipe go the other
//   way round (zen.workspaces.natural-scroll, off).
// - kWorkspacesScrollModifierKey: the key that makes a vertical wheel over
//   the sidebar switch Space (zen.workspaces.scroll-modifier-key): "ctrl"
//   (the default), "alt" (Option on macOS), "shift" or "meta" (Cmd on macOS).
//   A horizontal wheel needs none.
inline constexpr char kWorkspacesWrapAround[] =
    "koan.workspaces.wrap_around_navigation";
inline constexpr char kWorkspacesNaturalScroll[] =
    "koan.workspaces.natural_scroll";
inline constexpr char kWorkspacesScrollModifierKey[] =
    "koan.workspaces.scroll_modifier_key";

// Obsolete: until the tint became per window (a window's user colour
// override, KoanWorkspaceController::ApplySpaceColor()), Koan set the Space's
// colour as the profile's user colour and kept the user's own here (0: none).
// Read once to give a profile its own colour back, then cleared
// (MigrateWorkspaceTint()). Remove after 2027-03.
inline constexpr char kWorkspaceTintApplied[] = "koan.workspaces.tint_applied";
inline constexpr char kWorkspaceTintSavedColor[] =
    "koan.workspaces.tint_saved_color";
inline constexpr char kWorkspaceTintSavedVariant[] =
    "koan.workspaces.tint_saved_variant";

// The Spaces of the window the user was in last, so a window that opens
// without a session restore and without another window (startup set to the
// New Tab page or to pages; macOS's new window after the last one closed)
// gets them back (KoanWorkspaceController::RecreateStoredSpaces()): a list of
// {"title": "<icon> <name>", "color": tab_groups::TabGroupColorId,
// "active": bool, "pinned": [koan::StoredTab]}, in order: "pinned" is the
// Space's pinned tabs (not its folders' tabs, which kFolderList keeps), which
// come back unloaded with it (Zen restores only the pinned tabs when it
// doesn't resume the session). The session holds the Spaces otherwise (their
// tab groups).
inline constexpr char kWorkspaceList[] = "koan.workspaces.list";

// Zen's Glance settings (zen.glance.enabled, zen.glance.activation-method):
// whether Glance is on, and which modifier+click previews a link: "alt"
// (Zen's default on every platform, Option on macOS), "ctrl", "shift" or
// "meta" (Cmd on macOS). Renderers get them on their command line
// (koan_renderer_switches.h), so a change reaches pages in renderer
// processes started afterwards; the browser side applies it at once.
inline constexpr char kGlanceEnabled[] = "koan.glance.enabled";
inline constexpr char kGlanceActivationMethod[] =
    "koan.glance.activation_method";
// Zen's zen.glance.open-essential-external-links: a link from a pinned tab
// or Essential that opens a new tab on another site opens in Glance instead.
inline constexpr char kGlanceOpenPinnedExternalLinks[] =
    "koan.glance.open_essential_external_links";
// Zen's zen.glance.enable-contextmenu-search: the context menu's "Search
// ... for" opens its results in Glance.
inline constexpr char kGlanceContextMenuSearch[] =
    "koan.glance.enable_contextmenu_search";

// The user's own keyboard shortcuts, set in chrome://koan-settings: a
// dictionary from a shortcut's id (koan_shortcut_table.h) to its key, or to
// no key if the user cleared it. Actions not listed keep their default keys.
inline constexpr char kShortcuts[] = "koan.shortcuts";

// Zen's Tab Management settings (zen-default-prefs.js, with Zen's defaults).
// Closing the active tab selects the most recently used tab, not a
// neighbour, when the closed tab's opener doesn't decide
// (zen.tabs.select-recently-used-on-close).
inline constexpr char kSelectRecentlyUsedOnClose[] =
    "koan.tabs.select_recently_used_on_close";
// Back in a tab with no history closes it and returns to the tab that opened
// it (zen.tabs.close-on-back-with-no-history).
inline constexpr char kCloseOnBackWithNoHistory[] =
    "koan.tabs.close_on_back_with_no_history";
// What the URL bar opens from a pinned tab or Essential on another site goes
// to a new tab, so the pinned tab keeps its site
// (zen.tabs.open-pinned-in-new-tab).
inline constexpr char kOpenPinnedInNewTab[] =
    "koan.tabs.open_pinned_in_new_tab";
// Ctrl+Tab cycles within the Essentials (pinned tabs) or within the
// workspace's tabs, whichever the active tab belongs to
// (zen.tabs.ctrl-tab.ignore-essential-tabs).
inline constexpr char kCtrlTabWithinKind[] = "koan.tabs.ctrl_tab_within_kind";
// Ctrl+Tab skips tabs that haven't loaded yet (discarded or not yet
// restored; zen.tabs.ctrl-tab.ignore-pending-tabs).
inline constexpr char kCtrlTabSkipPending[] = "koan.tabs.ctrl_tab_skip_pending";

// Command palette: whether typing in the URL bar suggests matching actions
// (Zen's zen.urlbar.suggestions.quick-actions). The ">" list works either
// way.
inline constexpr char kCommandPaletteSuggestions[] =
    "koan.command_palette.suggestions";

// Command palette: how often each action was run (up to 5) or passed over
// (down to -5), by command id (Zen's zen.urlbar.suggestions-learner).
inline constexpr char kCommandPaletteUsage[] = "koan.command_palette.usage";

// What closing a pinned tab or an Essential does (Ctrl+W, its close button,
// a middle click), Zen's zen.pinned-tab-manager.close-shortcut-behavior:
// "close", "reset", "switch", "reset-switch", "unload-switch" or, the
// default, "reset-unload-switch" (back to the pinned URL, unload the tab and
// switch to the next one).
inline constexpr char kPinnedTabCloseShortcutBehavior[] =
    "koan.pinned_tabs.close_shortcut_behavior";

// Whether pinned tabs and Essentials come back at their pinned URL on
// startup, Zen's zen.pinned-tab-manager.restore-pinned-tabs-to-pinned-url
// (off by default).
inline constexpr char kPinnedTabRestoreToPinnedUrl[] =
    "koan.pinned_tabs.restore_to_pinned_url";

// The folders of the window the user is in, with their tabs (folder tabs are
// pinned tabs): restored, unloaded, in a window that opens without a session
// restore (startup set to the New Tab page or to pages), as Zen restores the
// pinned tabs and folders then. KoanFolderController keeps it.
inline constexpr char kFolderList[] = "koan.folders.list";
// The Library section shown last, opened next ("downloads", "spaces",
// "history"; Zen's zen.library.last-tab).
inline constexpr char kLibraryLastSection[] = "koan.library.last_section";
// Whether the Library's History lists closed tabs instead of history (Zen's
// zen.library.history.closed-tabs).
inline constexpr char kLibraryHistoryClosedTabs[] =
    "koan.library.history_closed_tabs";

// Zen's media controller in the sidebar (zen.mediacontrols.enabled).
inline constexpr char kMediaControlsEnabled[] = "koan.media_controls.enabled";

// Zen's download animation (zen.downloads.download-animation).
inline constexpr char kDownloadAnimation[] =
    "koan.downloads.download_animation";

// Dragging the window from empty space at the top of the page
// (zen.view.drag-window-from-content).
inline constexpr char kDragWindowFromContent[] =
    "koan.view.drag_window_from_content";

// Zen's Space routing (ZenSpaceRoutingManager.sys.mjs, its
// zen-space-routing.jsonlz4): the routes, a list of {"reference", "match_type"
// ("contains", "equal-to" or "regex"), "space" (a Space's name, "" for the
// most recent one)} in order (koan_space_routes.h), and the Space links from
// other apps open in when no route matches ("" for the most recent one).
inline constexpr char kSpaceRoutes[] = "koan.space_routing.routes";
inline constexpr char kSpaceRoutingExternalDefault[] =
    "koan.space_routing.external_default";
// Renaming any sidebar tab by double-clicking its label, and "Change Label…"
// in its menu (zen.tabs.rename-tabs).
inline constexpr char kRenameTabs[] = "koan.tabs.rename_tabs";
// The page-load progress bar above the selected tab's page
// (zen.view.enable-loading-indicator).
inline constexpr char kLoadingIndicator[] = "koan.view.loading_indicator";
// Whether sidebar tabs keep their favicon while loading instead of
// Chromium's spinner (zen.theme.hide-tab-throbber).
inline constexpr char kHideTabThrobber[] = "koan.theme.hide_tab_throbber";

// Whether the bookmarks bar may show at all. Zen has none, so Koan's default
// is no bar anywhere (also on the new tab page, and after an import, which
// would turn Chromium's own setting on); Koan's settings turn it on. Gated in
// BookmarkBarController::ShouldShowBookmarkBar() by a hook patch that reads
// this pref by name: keep the name in
// patches/chrome/browser/ui/bookmarks/bookmark_bar_controller.cc.patch.
inline constexpr char kBookmarksBarShow[] = "koan.bookmarks_bar.show";
// What a new tab page shows: "koan" (the default) is Koan's blank page in the
// theme's colours, like Zen's new tab; "chromium" is Chromium's own new tab
// page. Koan's own setting. A new tab page set by policy or by an extension
// wins over both.
inline constexpr char kNewTabPage[] = "koan.newtab.page";
inline constexpr char kNewTabPageKoan[] = "koan";
inline constexpr char kNewTabPageChromium[] = "chromium";

// Whether the welcome screen was shown (zen.welcome-screen.seen).
inline constexpr char kWelcomeScreenSeen[] = "koan.welcome_screen.seen";

// How many crashes in a row were restored silently (koan_crash_restore.h):
// 0 after a clean launch; and when the last silent restore was a while ago.
inline constexpr char kResumedCrashes[] = "koan.session.resumed_crashes";
// When the last silent crash restore happened.
inline constexpr char kLastResumedTime[] = "koan.session.last_resumed_time";

// Agent control (.agent/notes/agent-control.md section 6.1), local state.
// "Agent ready": the gateway listens. On by default in the default
// user-data-dir; off in Incognito, Guest and a non-default --user-data-dir
// launch unless the user turns it on there. Enterprise policy
// RemoteDebuggingAllowed=false keeps it off.
inline constexpr char kAgentControlEnabled[] = "koan.agent_control.enabled";
// The limits (koan/browser/agent/koan_agent_limits.h has the ranges): read
// with koan::agent::ReadLimit(), never directly.
inline constexpr char kAgentControlMaxLeasedTabs[] =
    "koan.agent_control.max_leased_tabs";
inline constexpr char kAgentControlLowPowerHalves[] =
    "koan.agent_control.low_power_halves";
inline constexpr char kAgentControlMaxPromptsPerMinute[] =
    "koan.agent_control.max_prompts_per_minute";
inline constexpr char kAgentControlMaxAuthFailures[] =
    "koan.agent_control.max_auth_failures";

// Whether Agent ready is on by default for this launch: the default
// user-data-dir, not --incognito, not --guest. Computed once, at registration.
bool AgentReadyByDefault();

// Agent control's strict mode (koan/browser/containers): a task that asks for
// no saved container runs in a hidden profile of its own, deleted when it
// ends. Profile pref; off by default.
inline constexpr char kAgentControlStrict[] = "koan.agent_control.strict";

// Agent control's saved containers (koan/browser/containers): their names
// and container ids, {name: id}. A throwaway container is never listed.
inline constexpr char kAgentSavedContainers[] =
    "koan.agent_control.saved_containers";

// Registers Koan's profile prefs. Called from
// chrome/browser/ui/tabs/tab_strip_prefs.cc.
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

// Changes the defaults of upstream profile prefs to Zen's: no remote search
// suggestions (Zen's browser.search.suggest.enabled is off, which also stops
// Chromium's remote zero-prefix suggestions, Google's trending searches), no
// AI Mode button in the omnibox, AI Mode switched off by its policy pref, and
// Global Privacy Control on, and with Koan's sidebar, Zen's sidebar width
// (kSidebarDefaultWidth).
// A value the user sets still wins. Called from
// chrome/browser/prefs/browser_prefs.cc at the end of RegisterProfilePrefs(),
// after upstream registered them.
void OverrideUpstreamProfilePrefDefaults(
    user_prefs::PrefRegistrySyncable* registry);

// The same for upstream profile prefs that keyed-service factories register,
// which happens after chrome::RegisterProfilePrefs() (crash note 8): no
// sign-in interception bubble. Called from
// chrome/browser/profiles/pref_service_builder_utils.cc after
// RegisterProfilePrefsForServices().
void OverrideServiceProfilePrefDefaults(
    user_prefs::PrefRegistrySyncable* registry);

// Registers Koan's local-state prefs (the look), and changes the defaults of
// upstream local-state prefs for Koan's memory behaviour (see
// docs/PERFORMANCE.md); a value the user sets in Settings > Performance
// still wins. Called from
// chrome/browser/prefs/browser_prefs.cc after upstream registered them.
void RegisterLocalStatePrefDefaults(PrefRegistrySimple* registry);

}  // namespace koan::prefs

#endif  // KOAN_BROWSER_KOAN_PREFS_H_

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

// Whether compact mode (auto-hiding sidebar and toolbar) is on. Applies to
// every window of the profile.
inline constexpr char kCompactModeEnabled[] = "koan.compact_mode.enabled";

// Zen's single-toolbar layout: no top toolbar; navigation and a URL pill at
// the top of the sidebar. On by default, like Zen.
inline constexpr char kSingleToolbarEnabled[] = "koan.single_toolbar";

// Whether the browser takes the active workspace's colour (Zen's
// per-workspace themes).
inline constexpr char kWorkspaceTint[] = "koan.workspaces.tint";

// The workspace tint Koan last applied as the browser's user colour (0:
// none), and the user's own colour from before it, so "All tabs" can restore
// it. Restoring only happens while Koan's tint is still the current colour,
// so a colour the user picks meanwhile is never overwritten.
// kWorkspaceTintSavedColor is 0 for "no user colour".
inline constexpr char kWorkspaceTintApplied[] = "koan.workspaces.tint_applied";
inline constexpr char kWorkspaceTintSavedColor[] =
    "koan.workspaces.tint_saved_color";
inline constexpr char kWorkspaceTintSavedVariant[] =
    "koan.workspaces.tint_saved_variant";

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
// Ctrl+Tab cycles within the Essentials (pinned tabs) or within the
// workspace's tabs, whichever the active tab belongs to
// (zen.tabs.ctrl-tab.ignore-essential-tabs).
inline constexpr char kCtrlTabWithinKind[] = "koan.tabs.ctrl_tab_within_kind";
// Ctrl+Tab skips tabs that haven't loaded yet (discarded or not yet
// restored; zen.tabs.ctrl-tab.ignore-pending-tabs).
inline constexpr char kCtrlTabSkipPending[] = "koan.tabs.ctrl_tab_skip_pending";

// Registers Koan's profile prefs. Called from
// chrome/browser/ui/tabs/tab_strip_prefs.cc.
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

// Changes the defaults of upstream local-state prefs for Koan's memory
// behaviour (see docs/PERFORMANCE.md); a value the user sets in Settings >
// Performance still wins. Called from
// chrome/browser/prefs/browser_prefs.cc after upstream registered them.
void RegisterLocalStatePrefDefaults(PrefRegistrySimple* registry);

}  // namespace koan::prefs

#endif  // KOAN_BROWSER_KOAN_PREFS_H_

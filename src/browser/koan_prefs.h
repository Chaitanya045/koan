// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_PREFS_H_
#define KOAN_BROWSER_KOAN_PREFS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace koan::prefs {

// Whether compact mode (auto-hiding sidebar and toolbar) is on. Applies to
// every window of the profile.
inline constexpr char kCompactModeEnabled[] = "koan.compact_mode.enabled";

// Zen's single-toolbar layout: no top toolbar; navigation and a URL pill at
// the top of the sidebar.
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

// Registers Koan's profile prefs. Called from
// chrome/browser/ui/tabs/tab_strip_prefs.cc.
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

}  // namespace koan::prefs

#endif  // KOAN_BROWSER_KOAN_PREFS_H_

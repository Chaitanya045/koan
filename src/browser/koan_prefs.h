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

// Whether the browser takes the active workspace's colour (Zen's
// per-workspace themes).
inline constexpr char kWorkspaceTint[] = "koan.workspaces.tint";

// The user's own colour, saved while a workspace tint is applied so that
// "All tabs" can restore it. kWorkspaceTintSaved says whether a value is
// saved; kWorkspaceTintSavedColor is 0 for "no user colour".
inline constexpr char kWorkspaceTintSaved[] = "koan.workspaces.tint_saved";
inline constexpr char kWorkspaceTintSavedColor[] =
    "koan.workspaces.tint_saved_color";
inline constexpr char kWorkspaceTintSavedVariant[] =
    "koan.workspaces.tint_saved_variant";

// Registers Koan's profile prefs. Called from
// chrome/browser/ui/tabs/tab_strip_prefs.cc.
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

}  // namespace koan::prefs

#endif  // KOAN_BROWSER_KOAN_PREFS_H_

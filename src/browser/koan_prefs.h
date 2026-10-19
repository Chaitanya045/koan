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

// Registers Koan's profile prefs. Called from
// chrome/browser/ui/tabs/tab_strip_prefs.cc.
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

}  // namespace koan::prefs

#endif  // KOAN_BROWSER_KOAN_PREFS_H_

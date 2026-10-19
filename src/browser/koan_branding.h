// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_KOAN_BRANDING_H_
#define KOAN_BROWSER_KOAN_BRANDING_H_

namespace koan {

// Koan ships without Google API keys on purpose (no Google sync, no Google
// cloud features), so the "Google API keys are missing" startup infobar is
// noise. Checked in chrome/browser/ui/startup/infobar_utils.cc.
inline constexpr bool kShowGoogleApiKeysInfoBar = false;

// Like Zen, Koan reopens the previous session on startup by default, so
// workspaces and Essentials survive restarts. Users can still change it in
// Settings > On startup. Checked in
// chrome/browser/prefs/session_startup_pref.cc.
inline constexpr bool kRestoreLastSessionByDefault = true;

}  // namespace koan

#endif  // KOAN_BROWSER_KOAN_BRANDING_H_

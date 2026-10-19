// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef KOAN_COMMON_KOAN_BRANDING_H_
#define KOAN_COMMON_KOAN_BRANDING_H_

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

// Linux desktop identity. The profile lives in ~/.config/koan (not
// ~/.config/chromium, so Koan and Chromium can coexist), the window's
// WM_CLASS / Wayland app_id matches koan.desktop, and the icon is "koan".
// Used by chrome/common/chrome_paths_linux.cc,
// chrome/common/channel_info_posix.cc and
// chrome/browser/shell_integration_linux.cc.
inline constexpr char kDataDirBasename[] = "koan";
inline constexpr char kDesktopFileName[] = "koan.desktop";
inline constexpr char kIconName[] = "koan";

}  // namespace koan

#endif  // KOAN_COMMON_KOAN_BRANDING_H_

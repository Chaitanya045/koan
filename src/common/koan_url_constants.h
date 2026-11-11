// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_COMMON_KOAN_URL_CONSTANTS_H_
#define KOAN_COMMON_KOAN_URL_CONSTANTS_H_

namespace koan {

// Koan's settings page, like Zen's about:preferences panes.
inline constexpr char kSettingsHost[] = "koan-settings";
inline constexpr char kSettingsURL[] = "chrome://koan-settings/";

// The welcome screen on first run (Zen's welcome flow), shown over the window
// (KoanWelcome).
inline constexpr char kWelcomeHost[] = "koan-welcome";
inline constexpr char kWelcomeURL[] = "chrome://koan-welcome/";

// Koan's new tab page, which chrome://newtab shows instead of Chromium's.
inline constexpr char kNewTabHost[] = "koan-newtab";
inline constexpr char kNewTabURL[] = "chrome://koan-newtab/";

}  // namespace koan

#endif  // KOAN_COMMON_KOAN_URL_CONSTANTS_H_

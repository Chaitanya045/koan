// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_CONTENT_RADIUS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_CONTENT_RADIUS_H_

#include <algorithm>

namespace koan {

// Zen's `--zen-squircle-value` on macOS and Linux (zen-theme.css).
inline constexpr float kZenSquircle = 1.3f;

// The corner radius of Zen's content card (zen-browser-container.css): its
// `--zen-native-inner-radius`, max(5px, (border radius - separation / 2) x
// squircle), divided by the squircle again, from the OS window's corner
// radius (`--zen-border-radius`, zenThemeModifier.js updateBorderRadius())
// and the gap around the card (`--zen-element-separation`).
constexpr float ZenContentCardRadius(int os_window_radius, int separation) {
  return std::max(5.f, (os_window_radius - separation / 2.f) * kZenSquircle) /
         kZenSquircle;
}

// Zen's `--zen-border-radius` for an OS: macOS's window corners, 10px, or
// 11px with macOS 26's (Tahoe's) design; 8px elsewhere.
constexpr int ZenOsWindowRadius(bool mac, int mac_major_version) {
  return mac ? (mac_major_version >= 26 ? 11 : 10) : 8;
}

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_CONTENT_RADIUS_H_

// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_COCOA_KOAN_SWIPE_H_
#define KOAN_BROWSER_UI_COCOA_KOAN_SWIPE_H_

namespace koan {

// Whether a two-finger swipe on a page goes back and forward only while
// System Settings > Trackpad > "Swipe between pages" is set to scroll with
// two fingers (NSEvent.swipeTrackingFromScrollEventsEnabled), as in Chromium.
// Zen swipes regardless (zen.widget.macos.override-system-swipe-gestures,
// true on macOS; nsCocoaUtils-mm.patch), and so does Koan: false. Read by
// chrome/browser/renderer_host/chrome_render_widget_host_view_mac_history_swiper.mm,
// which the browser and web app shims share, so this is header-only. Koan's
// swipe between Spaces over the sidebar never asked the setting.
inline bool PageSwipeFollowsSystemSetting() {
  return false;
}

}  // namespace koan

#endif  // KOAN_BROWSER_UI_COCOA_KOAN_SWIPE_H_

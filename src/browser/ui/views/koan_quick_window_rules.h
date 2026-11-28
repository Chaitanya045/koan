// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_QUICK_WINDOW_RULES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_QUICK_WINDOW_RULES_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "ui/gfx/geometry/rect.h"

// The quick window's rules (Arc's Little Arc, #472; koan_quick_window.h):
// pure, for the unit tests.
namespace koan::quick_window {

// What decides where a link from another app opens.
struct LinkFacts {
  // prefs::kExternalLinksOpenIn is kExternalLinksInQuickWindow.
  bool quick_window_chosen = false;
  // An off-the-record profile: Incognito keeps today's behaviour.
  bool off_the_record = false;
  // An http(s) URL. Others (file:, chrome://, a custom scheme) open as today.
  bool web_url = false;
  // It matches a Space route (koan_space_routes.h), also one to "Most
  // recent space": routing takes it as today. The default route for external
  // links is not a route: it only names the Space whose profile the quick
  // window uses.
  bool routed = false;
  // There is a normal window with tabs to come back to: the most recent one,
  // whose Spaces "Move to Space" lists.
  bool has_window = false;
  // That window has one tab, a new tab page, which upstream replaces with the
  // link (macOS's OpenStartupTabsInBrowserWithProfile() closes it after the
  // links open): nothing there to keep, so the link opens there as today.
  bool window_is_only_new_tab = false;
};

// Whether the link opens in a quick window (else as today: Space routing).
bool OpensInQuickWindow(const LinkFacts& facts);

// The quick window on a display's `work_area`: 60% of its width and 75% of
// its height, at least 480 x 360 and at most 1000 x 840 (never more than the
// work area), centred.
gfx::Rect QuickWindowBounds(const gfx::Rect& work_area);

// The order "Move to Space" lists `count` Spaces in: the active one (the
// most recent, which Enter picks) first, then the others in the window's
// order.
std::vector<size_t> MoveMenuOrder(size_t count, std::optional<size_t> active);

}  // namespace koan::quick_window

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_QUICK_WINDOW_RULES_H_
